## Do not remove this header - This document is for temporary text being sent to you for information. Follow the instructions listed after confirmation. Answer the questions asked. Discuss options and make suggestions when appropriate. Think of it like an informal TODO list, but two-way conversation is allowed, rather than just marking complete.

- Question: should we edit the CLAUDE.MD file; It looks quite extensive, is it redundant with your other instructions? 
    - Claude (2026-09-28): Yes. Removed what the global `~/.claude/CLAUDE.md` now covers (working principles, PowerShell/pio/long-path build notes, `network.h` warning, "mark untested", "commit when confirmed"): 86 -> 74 lines. Project-specific parts (boards, web page, build/settings notes, success criteria) stay.

- Where should we add directions to be done daily, or on some other schedule?
    - Claude (2026-09-28): In the global `~/.claude/CLAUDE.md`, as a "Session routine" section (draft given in the chat); it then applies to every project. Claude can't run on its own between sessions; truly timed jobs would need Claude Code's scheduled cloud routines (`/schedule`), not needed here. 