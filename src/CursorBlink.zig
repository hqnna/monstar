//! Cursor blink phase. While active, a periodic timerfd alternates the
//! cursor between its shown and hidden phases; while inactive the cursor is
//! always shown. The owner polls `fd` and calls `fire` when it is readable.

const CursorBlink = @This();

const std = @import("std");
const posix = std.posix;
const linux = std.os.linux;

const log = std.log.scoped(.cursor_blink);

/// Duration of each phase, matching Ghostty.
pub const interval_ms = 600;

fd: posix.fd_t,
/// Whether the blink timer is running.
active: bool = false,
/// The cursor is in its shown phase. Always true while inactive.
visible: bool = true,

pub fn init() error{TimerFdFailed}!CursorBlink {
    const rc = linux.timerfd_create(.MONOTONIC, .{ .CLOEXEC = true, .NONBLOCK = true });
    if (linux.errno(rc) != .SUCCESS) return error.TimerFdFailed;
    return .{ .fd = @intCast(rc) };
}

pub fn deinit(self: *CursorBlink) void {
    _ = linux.close(self.fd);
    self.* = undefined;
}

/// Start or stop blinking. Either transition restarts in the shown phase.
/// Returns true when the cursor's visibility changed.
pub fn sync(self: *CursorBlink, should_blink: bool) bool {
    if (should_blink == self.active) return false;
    self.active = should_blink;
    self.arm(should_blink);
    return self.show();
}

/// Restart the shown phase, as after typing, so the cursor does not vanish
/// while the user is looking for it. Returns true when visibility changed.
pub fn reset(self: *CursorBlink) bool {
    if (!self.active) return false;
    self.arm(true);
    return self.show();
}

/// Consume timer expirations. Returns true when the phase changed; an even
/// number of missed expirations leaves the phase where it was.
pub fn fire(self: *CursorBlink) bool {
    var expirations: u64 = 0;
    const n = posix.read(self.fd, std.mem.asBytes(&expirations)) catch return false;
    if (n != @sizeOf(u64) or !self.active) return false;
    if (expirations % 2 == 0) return false;
    self.visible = !self.visible;
    return true;
}

fn show(self: *CursorBlink) bool {
    const changed = !self.visible;
    self.visible = true;
    return changed;
}

fn arm(self: *CursorBlink, enabled: bool) void {
    const period: linux.timespec = if (enabled)
        .{ .sec = 0, .nsec = interval_ms * std.time.ns_per_ms }
    else
        .{ .sec = 0, .nsec = 0 };
    const spec: linux.itimerspec = .{ .it_value = period, .it_interval = period };
    const err = linux.errno(linux.timerfd_settime(self.fd, .{}, &spec, null));
    if (err != .SUCCESS) log.err("timerfd_settime failed: {}", .{err});
}

test "blinking starts and stops in the shown phase" {
    var blink: CursorBlink = try .init();
    defer blink.deinit();

    try std.testing.expect(!blink.sync(false));
    try std.testing.expect(!blink.sync(true));
    try std.testing.expect(blink.active and blink.visible);

    blink.visible = false;
    try std.testing.expect(blink.reset());
    try std.testing.expect(blink.visible);

    blink.visible = false;
    try std.testing.expect(blink.sync(false));
    try std.testing.expect(!blink.active and blink.visible);
    try std.testing.expect(!blink.reset());
}

test "fire toggles once per odd expiration count" {
    var blink: CursorBlink = try .init();
    defer blink.deinit();
    _ = blink.sync(true);

    // Nothing has expired yet: the nonblocking read fails.
    try std.testing.expect(!blink.fire());
    try std.testing.expect(blink.visible);

    // Expire immediately, once.
    const once: linux.itimerspec = .{
        .it_value = .{ .sec = 0, .nsec = 1 },
        .it_interval = .{ .sec = 0, .nsec = 0 },
    };
    try std.testing.expectEqual(.SUCCESS, linux.errno(linux.timerfd_settime(blink.fd, .{}, &once, null)));
    var pfd = [_]posix.pollfd{.{ .fd = blink.fd, .events = posix.POLL.IN, .revents = 0 }};
    _ = try posix.poll(&pfd, 1000);
    try std.testing.expect(blink.fire());
    try std.testing.expect(!blink.visible);
}
