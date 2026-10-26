//! Neovide-style animated cursor. When the cursor moves, each corner of the
//! drawn cursor travels from where it was to its new place. Corners leading
//! the motion arrive sooner than trailing ones, so the cursor stretches into
//! a trail and snaps back. A periodic timerfd paces frames while a trail is
//! in flight; the owner polls `fd` and redraws when `fire` returns true.

const CursorTrail = @This();

const std = @import("std");
const posix = std.posix;
const linux = std.os.linux;

const log = std.log.scoped(.cursor_trail);

/// About 120 frames per second; the compositor's frame callbacks still
/// throttle commits.
const frame_ns = std.time.ns_per_s / 120;

/// Corners clockwise from the top left, in grid-local physical pixels.
pub const Quad = [4][2]f64;

pub const Rect = struct {
    x: f64,
    y: f64,
    width: f64,
    height: f64,

    fn corners(self: Rect) Quad {
        return .{
            .{ self.x, self.y },
            .{ self.x + self.width, self.y },
            .{ self.x + self.width, self.y + self.height },
            .{ self.x, self.y + self.height },
        };
    }

    fn center(self: Rect) [2]f64 {
        return .{ self.x + self.width / 2, self.y + self.height / 2 };
    }
};

/// Unit directions from a cursor's center toward each corner.
const corner_directions: Quad = .{ .{ -1, -1 }, .{ 1, -1 }, .{ 1, 1 }, .{ -1, 1 } };

fd: posix.fd_t,
/// Where the cursor is headed; null before the first frame.
target: ?Rect = null,
from: Quad = undefined,
/// Monotonic start of the current motion; null when the cursor is at rest.
start_ns: ?i96 = null,
/// Each corner's travel time.
durations_ns: [4]f64 = @splat(0),
ticking: bool = false,

pub fn init() error{TimerFdFailed}!CursorTrail {
    const rc = linux.timerfd_create(.MONOTONIC, .{ .CLOEXEC = true, .NONBLOCK = true });
    if (linux.errno(rc) != .SUCCESS) return error.TimerFdFailed;
    return .{ .fd = @intCast(rc) };
}

pub fn deinit(self: *CursorTrail) void {
    _ = linux.close(self.fd);
    self.* = undefined;
}

/// Move toward `rect`. `duration_ms` is the trailing corners' travel time;
/// `size`, from 0 to 1, is how much sooner the leading corners arrive. A
/// zero duration or a hidden cursor (null) disables the animation.
pub fn update(self: *CursorTrail, rect: ?Rect, now_ns: i96, duration_ms: u32, size: f64) void {
    std.debug.assert(size >= 0 and size <= 1);
    const new = rect orelse return self.reset();
    if (duration_ms == 0) return self.reset();
    const old = self.target orelse {
        self.target = new;
        return;
    };
    if (std.meta.eql(old, new)) return;

    // Continue from where the corners are now, so a cursor that moves again
    // mid-flight does not jump.
    self.from = self.quadAt(now_ns) orelse old.corners();
    self.target = new;
    self.start_ns = now_ns;

    const from_center = centerOf(self.from);
    const to_center = new.center();
    const dx = to_center[0] - from_center[0];
    const dy = to_center[1] - from_center[1];
    // Rank corners by how well they face the motion: the most aligned
    // leads, the least aligned trails, the rest are in between.
    var alignment: [4]f64 = undefined;
    for (corner_directions, 0..) |dir, i| alignment[i] = dir[0] * dx + dir[1] * dy;
    const full: f64 = @floatFromInt(@as(u64, duration_ms) * std.time.ns_per_ms);
    for (0..4) |i| {
        var rank: f64 = 0;
        for (0..4) |j| {
            if (alignment[j] > alignment[i] or (alignment[j] == alignment[i] and j < i)) rank += 1;
        }
        const lead = 1 - size * (1 - rank / 3);
        self.durations_ns[i] = full * lead;
    }
    self.arm(true);
}

/// The quad to draw, or null when the cursor is at rest and should be drawn
/// normally.
pub fn quadAt(self: *const CursorTrail, now_ns: i96) ?Quad {
    const start = self.start_ns orelse return null;
    const target = (self.target orelse return null).corners();
    const elapsed: f64 = @floatFromInt(@max(0, now_ns - start));
    var moving = false;
    var quad: Quad = undefined;
    for (0..4) |i| {
        const duration = self.durations_ns[i];
        const t = if (duration <= 0) 1 else @min(1, elapsed / duration);
        if (t < 1) moving = true;
        // Ease out: fast start, gentle arrival.
        const eased = 1 - std.math.pow(f64, 1 - t, 3);
        for (0..2) |axis| {
            quad[i][axis] = self.from[i][axis] + (target[i][axis] - self.from[i][axis]) * eased;
        }
    }
    return if (moving) quad else null;
}

/// Consume frame-timer expirations. Returns true while a frame is due; the
/// timer stops once the motion has finished.
pub fn fire(self: *CursorTrail, now_ns: i96) bool {
    var expirations: u64 = 0;
    _ = posix.read(self.fd, std.mem.asBytes(&expirations)) catch return false;
    if (self.quadAt(now_ns) == null) {
        self.start_ns = null;
        self.arm(false);
    }
    // One more frame draws the resting cursor.
    return true;
}

fn reset(self: *CursorTrail) void {
    self.target = null;
    self.start_ns = null;
    self.arm(false);
}

fn centerOf(quad: Quad) [2]f64 {
    var sum: [2]f64 = .{ 0, 0 };
    for (quad) |corner| {
        sum[0] += corner[0];
        sum[1] += corner[1];
    }
    return .{ sum[0] / 4, sum[1] / 4 };
}

fn arm(self: *CursorTrail, enabled: bool) void {
    if (enabled == self.ticking) return;
    self.ticking = enabled;
    const period: linux.timespec = if (enabled) .{ .sec = 0, .nsec = frame_ns } else .{ .sec = 0, .nsec = 0 };
    const spec: linux.itimerspec = .{ .it_value = period, .it_interval = period };
    const err = linux.errno(linux.timerfd_settime(self.fd, .{}, &spec, null));
    if (err != .SUCCESS) log.err("timerfd_settime failed: {}", .{err});
}

test "the first position and unchanged targets do not animate" {
    var trail: CursorTrail = .{ .fd = -1 };
    const rect: Rect = .{ .x = 0, .y = 0, .width = 10, .height = 20 };
    trail.update(rect, 0, 150, 0.8);
    try std.testing.expectEqual(null, trail.quadAt(0));
    trail.update(rect, 10, 150, 0.8);
    try std.testing.expectEqual(null, trail.quadAt(10));
}

test "leading corners arrive before trailing corners" {
    var trail: CursorTrail = try .init();
    defer trail.deinit();
    trail.update(.{ .x = 0, .y = 0, .width = 10, .height = 20 }, 0, 100, 1);
    // Move right: the right-hand corners lead.
    trail.update(.{ .x = 100, .y = 0, .width = 10, .height = 20 }, 0, 100, 1);
    const ms = std.time.ns_per_ms;
    const mid = trail.quadAt(10 * ms).?;
    try std.testing.expect(mid[1][0] > mid[0][0] + 10);
    try std.testing.expect(mid[2][0] > mid[3][0] + 10);
    // With size 1 the leading corners arrive at once.
    try std.testing.expectEqual(@as(f64, 110), mid[1][0]);
    try std.testing.expectEqual(null, trail.quadAt(100 * ms));
}

test "a zero size moves the cursor without stretching" {
    var trail: CursorTrail = try .init();
    defer trail.deinit();
    trail.update(.{ .x = 0, .y = 0, .width = 10, .height = 20 }, 0, 100, 0);
    trail.update(.{ .x = 0, .y = 200, .width = 10, .height = 20 }, 0, 100, 0);
    const mid = trail.quadAt(50 * std.time.ns_per_ms).?;
    try std.testing.expectApproxEqAbs(@as(f64, 20), mid[3][1] - mid[0][1], 1e-9);
}

test "retargeting continues from the current corners and disabling resets" {
    var trail: CursorTrail = try .init();
    defer trail.deinit();
    const ms = std.time.ns_per_ms;
    trail.update(.{ .x = 0, .y = 0, .width = 10, .height = 20 }, 0, 100, 0);
    trail.update(.{ .x = 100, .y = 0, .width = 10, .height = 20 }, 0, 100, 0);
    const before = trail.quadAt(50 * ms).?;
    trail.update(.{ .x = 100, .y = 100, .width = 10, .height = 20 }, 50 * ms, 100, 0);
    const after = trail.quadAt(50 * ms + 1).?;
    try std.testing.expectApproxEqAbs(before[0][0], after[0][0], 0.01);
    trail.update(null, 60 * ms, 100, 0);
    try std.testing.expectEqual(null, trail.quadAt(60 * ms));
    try std.testing.expect(!trail.ticking);
}
