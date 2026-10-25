//! Single-key Ghostty-style bindings. User entries override defaults by trigger;
//! lookup prefers physical keys, then generated text, then unshifted text.
//! `unbind` entries suppress defaults, including App's remaining fixed shortcuts.
//! Tab and split actions belong to a hosting application such as monstar-qt;
//! without one, their keys reach the terminal application unchanged.
//!
//! Trigger parsing and Unicode comparison adapted from Ghostty's
//! src/input/Binding.zig at 8144ef4e73e70a4e9942fceb319819005f07fd37.

// MIT License
// Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

const std = @import("std");
const vt = @import("ghostty-vt");
const uucode = @import("uucode");

pub const Action = union(enum) {
    new_window,
    scroll_page_lines: i16,
    unbind,
    host: HostAction,
};

pub const SplitDirection = enum { right, down, left, up };
pub const SplitFocus = enum { previous, next, up, left, down, right };
pub const ResizeDirection = enum { up, down, left, right };

/// Window-management actions performed by a hosting application. The text
/// form is Ghostty's action syntax and is what hosts receive.
pub const HostAction = union(enum) {
    new_tab,
    close_tab,
    previous_tab,
    next_tab,
    last_tab,
    /// One-based tab index.
    goto_tab: u16,
    /// Relative tab movement; negative moves left.
    move_tab: i16,
    new_split: SplitDirection,
    goto_split: SplitFocus,
    toggle_split_zoom,
    equalize_splits,
    resize_split: struct { direction: ResizeDirection, amount: u16 },
    close_surface,

    pub fn parse(text: []const u8) error{InvalidValue}!HostAction {
        const colon = std.mem.indexOfScalar(u8, text, ':');
        const name = text[0 .. colon orelse text.len];
        const param: ?[]const u8 = if (colon) |idx| text[idx + 1 ..] else null;
        inline for (@typeInfo(HostAction).@"union".fields) |field| {
            if (std.mem.eql(u8, name, field.name)) {
                if (field.type == void) {
                    if (param != null) return error.InvalidValue;
                    return @unionInit(HostAction, field.name, {});
                }
                const value = param orelse return error.InvalidValue;
                return @unionInit(HostAction, field.name, try parseParam(field.type, value));
            }
        }
        return error.InvalidValue;
    }

    fn parseParam(comptime T: type, value: []const u8) error{InvalidValue}!T {
        switch (@typeInfo(T)) {
            .@"enum" => return std.meta.stringToEnum(T, value) orelse error.InvalidValue,
            .int => return std.fmt.parseInt(T, value, 10) catch error.InvalidValue,
            .@"struct" => {
                const comma = std.mem.indexOfScalar(u8, value, ',') orelse return error.InvalidValue;
                return .{
                    .direction = try parseParam(ResizeDirection, value[0..comma]),
                    .amount = try parseParam(u16, value[comma + 1 ..]),
                };
            },
            else => @compileError("unsupported host action parameter"),
        }
    }

    /// Write the action in Ghostty's syntax, which `parse` accepts.
    pub fn format(self: HostAction, writer: *std.Io.Writer) std.Io.Writer.Error!void {
        try writer.writeAll(@tagName(self));
        switch (self) {
            .goto_tab => |index| try writer.print(":{d}", .{index}),
            .move_tab => |offset| try writer.print(":{d}", .{offset}),
            .new_split => |direction| try writer.print(":{s}", .{@tagName(direction)}),
            .goto_split => |focus| try writer.print(":{s}", .{@tagName(focus)}),
            .resize_split => |resize| try writer.print(":{s},{d}", .{ @tagName(resize.direction), resize.amount }),
            else => {},
        }
    }
};

pub const Binding = struct {
    trigger: Trigger,
    action: Action,
};

pub const Trigger = struct {
    key: union(enum) { physical: vt.input.Key, unicode: u21 },
    mods: vt.input.KeyMods = .{},

    fn equal(a: Trigger, b: Trigger) bool {
        if (!a.mods.binding().equal(b.mods.binding())) return false;
        if (std.meta.activeTag(a.key) != std.meta.activeTag(b.key)) return false;
        return switch (a.key) {
            .physical => |key| key == b.key.physical,
            .unicode => |cp| std.mem.eql(u21, &foldedCodepoint(cp), &foldedCodepoint(b.key.unicode)),
        };
    }
};

const ctrl_shift: vt.input.KeyMods = .{ .ctrl = true, .shift = true };
const ctrl_alt: vt.input.KeyMods = .{ .ctrl = true, .alt = true };
const ctrl_super: vt.input.KeyMods = .{ .ctrl = true, .super = true };
const ctrl_super_shift: vt.input.KeyMods = .{ .ctrl = true, .super = true, .shift = true };

/// Ghostty's Linux defaults for the actions Monstar supports.
const defaults = [_]Binding{
    .{ .trigger = .{ .key = .{ .physical = .arrow_up }, .mods = .{ .shift = true } }, .action = .{ .scroll_page_lines = -1 } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_down }, .mods = .{ .shift = true } }, .action = .{ .scroll_page_lines = 1 } },
    .{ .trigger = .{ .key = .{ .unicode = 'n' }, .mods = ctrl_shift }, .action = .new_window },
    .{ .trigger = .{ .key = .{ .unicode = 't' }, .mods = ctrl_shift }, .action = .{ .host = .new_tab } },
    .{ .trigger = .{ .key = .{ .unicode = 'w' }, .mods = ctrl_shift }, .action = .{ .host = .close_surface } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_left }, .mods = ctrl_shift }, .action = .{ .host = .previous_tab } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_right }, .mods = ctrl_shift }, .action = .{ .host = .next_tab } },
    .{ .trigger = .{ .key = .{ .physical = .page_up }, .mods = .{ .ctrl = true } }, .action = .{ .host = .previous_tab } },
    .{ .trigger = .{ .key = .{ .physical = .page_down }, .mods = .{ .ctrl = true } }, .action = .{ .host = .next_tab } },
    .{ .trigger = .{ .key = .{ .physical = .tab }, .mods = ctrl_shift }, .action = .{ .host = .previous_tab } },
    .{ .trigger = .{ .key = .{ .physical = .tab }, .mods = .{ .ctrl = true } }, .action = .{ .host = .next_tab } },
    .{ .trigger = .{ .key = .{ .physical = .page_up }, .mods = ctrl_shift }, .action = .{ .host = .{ .move_tab = -1 } } },
    .{ .trigger = .{ .key = .{ .physical = .page_down }, .mods = ctrl_shift }, .action = .{ .host = .{ .move_tab = 1 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_1 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 1 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_2 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 2 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_3 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 3 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_4 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 4 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_5 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 5 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_6 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 6 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_7 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 7 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_8 }, .mods = .{ .alt = true } }, .action = .{ .host = .{ .goto_tab = 8 } } },
    .{ .trigger = .{ .key = .{ .physical = .digit_9 }, .mods = .{ .alt = true } }, .action = .{ .host = .last_tab } },
    .{ .trigger = .{ .key = .{ .unicode = 'o' }, .mods = ctrl_shift }, .action = .{ .host = .{ .new_split = .right } } },
    .{ .trigger = .{ .key = .{ .unicode = 'e' }, .mods = ctrl_shift }, .action = .{ .host = .{ .new_split = .down } } },
    .{ .trigger = .{ .key = .{ .physical = .bracket_left }, .mods = ctrl_super }, .action = .{ .host = .{ .goto_split = .previous } } },
    .{ .trigger = .{ .key = .{ .physical = .bracket_right }, .mods = ctrl_super }, .action = .{ .host = .{ .goto_split = .next } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_up }, .mods = ctrl_alt }, .action = .{ .host = .{ .goto_split = .up } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_down }, .mods = ctrl_alt }, .action = .{ .host = .{ .goto_split = .down } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_left }, .mods = ctrl_alt }, .action = .{ .host = .{ .goto_split = .left } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_right }, .mods = ctrl_alt }, .action = .{ .host = .{ .goto_split = .right } } },
    .{ .trigger = .{ .key = .{ .physical = .enter }, .mods = ctrl_shift }, .action = .{ .host = .toggle_split_zoom } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_up }, .mods = ctrl_super_shift }, .action = .{ .host = .{ .resize_split = .{ .direction = .up, .amount = 10 } } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_down }, .mods = ctrl_super_shift }, .action = .{ .host = .{ .resize_split = .{ .direction = .down, .amount = 10 } } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_left }, .mods = ctrl_super_shift }, .action = .{ .host = .{ .resize_split = .{ .direction = .left, .amount = 10 } } } },
    .{ .trigger = .{ .key = .{ .physical = .arrow_right }, .mods = ctrl_super_shift }, .action = .{ .host = .{ .resize_split = .{ .direction = .right, .amount = 10 } } } },
    .{ .trigger = .{ .key = .{ .physical = .equal }, .mods = ctrl_super_shift }, .action = .{ .host = .equalize_splits } },
};

/// Parse without allocating. Only single-key triggers and the actions in Action
/// are supported; unsupported Ghostty sequences, flags and actions are errors.
pub fn parse(input: []const u8) error{InvalidValue}!Binding {
    // Skip '=' when it is the trigger key, e.g. ctrl+==scroll_page_lines:-1
    // or =+ctrl=scroll_page_lines:-1 (the key need not follow the modifiers).
    var offset: usize = 0;
    const eq = while (std.mem.indexOfScalar(u8, input[offset..], '=')) |relative| {
        const idx = offset + relative;
        if (idx + 1 < input.len and (input[idx + 1] == '+' or input[idx + 1] == '=')) {
            offset = idx + 1;
            continue;
        }
        break idx;
    } else return error.InvalidValue;

    const action_text = std.mem.trim(u8, input[eq + 1 ..], " \t");
    const action: Action = if (std.mem.eql(u8, action_text, "unbind"))
        .unbind
    else if (std.mem.eql(u8, action_text, "new_window"))
        .new_window
    else if (std.mem.startsWith(u8, action_text, "scroll_page_lines:"))
        .{ .scroll_page_lines = std.fmt.parseInt(i16, action_text["scroll_page_lines:".len..], 10) catch return error.InvalidValue }
    else
        .{ .host = try HostAction.parse(action_text) };
    return .{ .trigger = try parseTrigger(std.mem.trim(u8, input[0..eq], " \t")), .action = action };
}

fn parseTrigger(input: []const u8) error{InvalidValue}!Trigger {
    var key: ?@FieldType(Trigger, "key") = null;
    var mods: vt.input.KeyMods = .{};
    var rem = input;
    loop: while (rem.len > 0) {
        const idx = std.mem.indexOfScalar(u8, rem, '+') orelse rem.len;
        const part = rem[0..idx];
        rem = if (idx >= rem.len) "" else rem[idx + 1 ..];

        inline for (.{ "shift", "ctrl", "alt", "super" }) |name| {
            if (std.mem.eql(u8, part, name)) {
                if (@field(mods, name)) return error.InvalidValue;
                @field(mods, name) = true;
                continue :loop;
            }
        }
        inline for (.{
            .{ "control", "ctrl" },
            .{ "opt", "alt" },
            .{ "option", "alt" },
            .{ "cmd", "super" },
            .{ "command", "super" },
        }) |pair| {
            if (std.mem.eql(u8, part, pair[0])) {
                if (@field(mods, pair[1])) return error.InvalidValue;
                @field(mods, pair[1]) = true;
                continue :loop;
            }
        }

        if (key != null) return error.InvalidValue;
        if (part.len == 0) {
            key = .{ .unicode = '+' };
        } else if (singleCodepoint(part)) |cp| {
            key = .{ .unicode = cp };
        } else if (vt.input.Key.fromW3C(part)) |physical| {
            if (physical == .unidentified) return error.InvalidValue;
            key = .{ .physical = physical };
        } else {
            // Ghostty's common pre-W3C navigation aliases.
            const physical = std.StaticStringMap(vt.input.Key).initComptime(.{
                .{ "up", .arrow_up },
                .{ "down", .arrow_down },
                .{ "left", .arrow_left },
                .{ "right", .arrow_right },
            }).get(part) orelse return error.InvalidValue;
            key = .{ .physical = physical };
        }
    }
    return .{ .key = key orelse return error.InvalidValue, .mods = mods };
}

fn singleCodepoint(text: []const u8) ?u21 {
    const view = std.unicode.Utf8View.init(text) catch return null;
    var it = view.iterator();
    const cp = it.nextCodepoint() orelse return null;
    return if (it.nextCodepoint() == null) cp else null;
}

fn foldedCodepoint(cp: u21) [3]u21 {
    if (uucode.ascii.isAlphabetic(cp)) return .{ uucode.ascii.toLower(cp), 0, 0 };
    var buffer: [1]u21 = undefined;
    const slice = uucode.get(.case_folding_full, cp).with(&buffer, cp);
    var result: [3]u21 = @splat(0);
    std.debug.assert(slice.len <= result.len);
    @memcpy(result[0..slice.len], slice);
    return result;
}

/// Replace an existing trigger or append a new one. Invalid input and allocation
/// failure leave the list unchanged. Storage belongs to the supplied allocator.
pub fn put(bindings: *std.ArrayList(Binding), alloc: std.mem.Allocator, input: []const u8) error{ InvalidValue, OutOfMemory }!void {
    const binding = try parse(input);
    for (bindings.items) |*existing| {
        if (existing.trigger.equal(binding.trigger)) {
            existing.* = binding;
            return;
        }
    }
    try bindings.append(alloc, binding);
}

fn get(bindings: []const Binding, trigger: Trigger) ?Action {
    for (bindings) |binding| {
        if (binding.trigger.equal(trigger)) return binding.action;
    }
    for (defaults) |binding| {
        if (binding.trigger.equal(trigger)) return binding.action;
    }
    return null;
}

/// Return a matching action, or `unbind` when the event should bypass fixed
/// shortcuts. Lock keys, modifier sides and consumed modifiers do not alter
/// matching. Unbinding a physical trigger still permits a Unicode binding.
pub fn getEvent(bindings: []const Binding, event: vt.input.KeyEvent) ?Action {
    const keys = [_]?@FieldType(Trigger, "key"){
        .{ .physical = event.key },
        if (singleCodepoint(event.utf8)) |cp| .{ .unicode = cp } else null,
        if (event.unshifted_codepoint > 0) .{ .unicode = event.unshifted_codepoint } else null,
    };
    var unbound = false;
    for (keys) |maybe_key| {
        const key = maybe_key orelse continue;
        const action = get(bindings, .{ .key = key, .mods = event.mods.binding() }) orelse continue;
        if (action == .unbind) {
            unbound = true;
            continue;
        }
        return action;
    }
    return if (unbound) .unbind else null;
}

test "Ghostty single-key syntax and action parameters" {
    const cases = [_]struct { text: []const u8, key: @FieldType(Trigger, "key"), mods: vt.input.KeyMods }{
        .{ .text = "ctrl+shift+k", .key = .{ .unicode = 'k' }, .mods = .{ .ctrl = true, .shift = true } },
        .{ .text = "option+ö", .key = .{ .unicode = 'ö' }, .mods = .{ .alt = true } },
        .{ .text = "command+PageUp", .key = .{ .physical = .page_up }, .mods = .{ .super = true } },
        .{ .text = "control+KeyK", .key = .{ .physical = .key_k }, .mods = .{ .ctrl = true } },
        .{ .text = "shift+up", .key = .{ .physical = .arrow_up }, .mods = .{ .shift = true } },
        .{ .text = "ctrl++", .key = .{ .unicode = '+' }, .mods = .{ .ctrl = true } },
        .{ .text = "=+ctrl", .key = .{ .unicode = '=' }, .mods = .{ .ctrl = true } },
        .{ .text = "ctrl+=", .key = .{ .unicode = '=' }, .mods = .{ .ctrl = true } },
        .{ .text = "shift+?", .key = .{ .unicode = '?' }, .mods = .{ .shift = true } },
    };
    for (cases) |case| {
        var buf: [128]u8 = undefined;
        const binding = try parse(try std.fmt.bufPrint(&buf, "{s}=scroll_page_lines:-5", .{case.text}));
        try std.testing.expectEqualDeep(case.key, binding.trigger.key);
        try std.testing.expectEqual(case.mods, binding.trigger.mods);
        try std.testing.expectEqual(@as(i16, -5), binding.action.scroll_page_lines);
    }
    try std.testing.expectEqual(@as(i16, -32768), (try parse("a=scroll_page_lines:-32768")).action.scroll_page_lines);
    try std.testing.expectEqual(@as(i16, 32767), (try parse("a=scroll_page_lines:32767")).action.scroll_page_lines);
    try std.testing.expectEqual(@as(i16, 0), (try parse("a=scroll_page_lines:0")).action.scroll_page_lines);
    for ([_][]const u8{
        "",                    "ctrl=unbind",               "ctrl+control+a=unbind",      "a+b=unbind",              "=unbind",
        "a=scroll_page_lines", "a=scroll_page_lines:32768", "a=scroll_page_lines:-32769", "a=scroll_page_lines:1.5", "a=scroll_page_lines:",
        "a=unknown",           "a=unbind:1",                "a=new_window:1",             "ctrl+not-a-key=unbind",   "a>b=unbind",
        "global:a=unbind",     "chain=unbind",              "catch_all=unbind",           "\xff=unbind",
    }) |text| try std.testing.expectError(error.InvalidValue, parse(text));
}

test "host actions parse and format in Ghostty syntax" {
    for ([_][]const u8{
        "new_tab",            "close_tab",             "previous_tab",      "next_tab",
        "last_tab",           "goto_tab:3",            "move_tab:-1",       "new_split:right",
        "new_split:down",     "new_split:left",        "new_split:up",      "goto_split:previous",
        "goto_split:next",    "goto_split:left",       "toggle_split_zoom", "equalize_splits",
        "resize_split:up,10", "resize_split:right,25", "close_surface",
    }) |text| {
        var input_buf: [64]u8 = undefined;
        const binding = try parse(try std.fmt.bufPrint(&input_buf, "ctrl+a={s}", .{text}));
        var buf: [64]u8 = undefined;
        var writer: std.Io.Writer = .fixed(&buf);
        try binding.action.host.format(&writer);
        try std.testing.expectEqualStrings(text, writer.buffered());
    }
    for ([_][]const u8{
        "a=new_tab:1",          "a=goto_tab",          "a=goto_tab:x",          "a=goto_tab:-1",
        "a=new_split",          "a=new_split:auto",    "a=goto_split:top",      "a=resize_split:up",
        "a=resize_split:up,-1", "a=resize_split:in,1", "a=toggle_split_zoom:1", "a=move_tab:",
    }) |text| try std.testing.expectError(error.InvalidValue, parse(text));
}

test "Ghostty's tab and split defaults resolve to host actions" {
    const cases = [_]struct { event: vt.input.KeyEvent, action: HostAction }{
        .{ .event = .{ .key = .key_t, .utf8 = "T", .unshifted_codepoint = 't', .mods = ctrl_shift }, .action = .new_tab },
        .{ .event = .{ .key = .key_o, .unshifted_codepoint = 'o', .mods = ctrl_shift }, .action = .{ .new_split = .right } },
        .{ .event = .{ .key = .key_e, .unshifted_codepoint = 'e', .mods = ctrl_shift }, .action = .{ .new_split = .down } },
        .{ .event = .{ .key = .arrow_left, .mods = ctrl_alt }, .action = .{ .goto_split = .left } },
        .{ .event = .{ .key = .digit_3, .unshifted_codepoint = '3', .mods = .{ .alt = true } }, .action = .{ .goto_tab = 3 } },
        .{ .event = .{ .key = .tab, .mods = .{ .ctrl = true } }, .action = .next_tab },
    };
    for (cases) |case| {
        try std.testing.expectEqualDeep(Action{ .host = case.action }, getEvent(&.{}, case.event).?);
    }
}

test "repeatable bindings replace, unbind and restore without losing other triggers" {
    var bindings: std.ArrayList(Binding) = .empty;
    defer bindings.deinit(std.testing.allocator);
    try put(&bindings, std.testing.allocator, "alt+j=scroll_page_lines:1");
    try put(&bindings, std.testing.allocator, "alt+k=scroll_page_lines:-1");
    try put(&bindings, std.testing.allocator, "alt+J=scroll_page_lines:5");
    try std.testing.expectEqual(@as(usize, 2), bindings.items.len);
    const event: vt.input.KeyEvent = .{ .key = .key_j, .unshifted_codepoint = 'j', .mods = .{ .alt = true } };
    try std.testing.expectEqual(@as(i16, 5), getEvent(bindings.items, event).?.scroll_page_lines);
    try std.testing.expectError(error.InvalidValue, put(&bindings, std.testing.allocator, "alt+j=scroll_page_lines:no"));
    try std.testing.expectEqual(@as(i16, 5), getEvent(bindings.items, event).?.scroll_page_lines);
    try put(&bindings, std.testing.allocator, "alt+j=unbind");
    try std.testing.expectEqual(Action.unbind, getEvent(bindings.items, event).?);
    try put(&bindings, std.testing.allocator, "alt+j=scroll_page_lines:2");
    try std.testing.expectEqual(@as(i16, 2), getEvent(bindings.items, event).?.scroll_page_lines);
    try put(&bindings, std.testing.allocator, "shift+up=unbind");
    try std.testing.expectEqual(Action.unbind, getEvent(bindings.items, .{ .key = .arrow_up, .mods = .{ .shift = true } }).?);
}

test "physical then generated then unshifted matching with exact modifiers" {
    var bindings: std.ArrayList(Binding) = .empty;
    defer bindings.deinit(std.testing.allocator);
    try put(&bindings, std.testing.allocator, "ctrl+shift+KeyK=scroll_page_lines:3");
    try put(&bindings, std.testing.allocator, "ctrl+shift+?=scroll_page_lines:2");
    try put(&bindings, std.testing.allocator, "ctrl+shift+ö=scroll_page_lines:1");
    var event: vt.input.KeyEvent = .{
        .key = .key_k,
        .utf8 = "?",
        .unshifted_codepoint = 'ö',
        .mods = .{ .ctrl = true, .shift = true, .caps_lock = true, .num_lock = true, .sides = .{ .ctrl = .right } },
        .consumed_mods = .{ .shift = true },
    };
    try std.testing.expectEqual(@as(i16, 3), getEvent(bindings.items, event).?.scroll_page_lines);
    try put(&bindings, std.testing.allocator, "ctrl+shift+KeyK=unbind");
    try std.testing.expectEqual(@as(i16, 2), getEvent(bindings.items, event).?.scroll_page_lines);
    event.utf8 = "Ö";
    try std.testing.expectEqual(@as(i16, 1), getEvent(bindings.items, event).?.scroll_page_lines);
    event.utf8 = "multiple codepoints";
    try std.testing.expectEqual(@as(i16, 1), getEvent(bindings.items, event).?.scroll_page_lines);
    event.mods.alt = true;
    try std.testing.expectEqual(null, getEvent(bindings.items, event));
}
