/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// !! COMMENTS IN THIS FILE STAY! You may add new ones

// Blackbox Module.
// Crash Trace Functionality

// Scenario: Oh no! The engine writes out of a legal buffer, seizes up, and crashes! The logger is stuck in a spinlock and the queue never get's drain()'d, so no last message can be sent to tell the world what happened... unless?

// Enter: anoptic_log_crash.h

// Stage 1: Initializing the blackbox.
// Captures the SIGSEGV, SIGABRT, SIGFPE, etc.
// Loads up the blackbox infrastructure.

// Stage 2: Hopefully never, but...
// A crash occurs. Immediately write trace to CRASH.log

// Stage 3: Immediately following stage 2, a hail mary log_flush() to hopefully recover anything that might've survived in the log buffers, *after* we have already written the CRASH.log.

// Stage 4: Final, the anocraft accident investigation.
// TODO: Ask user if they want to send telemetry (future implement)
// First boot post-crash, investigate what happened, append it to logs in further details if necessary/possible/desireable.

#pragma once

#include "anoptic_log.h"

namespace ano {


/* Lifecycle Functions */

// Arm the blackbox: run the Stage 4 check for a previous run's CRASH.log, then install the Stage 1 hooks.
// Call once from main, right after log_init. The hooks live for the whole process, no cleanup.
// The record file is per-session -- <gamedir>/logs/<session-stamp>_CRASH.log, the stamp shared with
// the logger's own file (fs_session_stamp) -- resolved once here, never inside a handler.
// Stage 4 announces how many *_CRASH.log files are left over ("n crash logs detected"), then prunes
// both *_CRASH.log and *_ano.log to the newest 4 each, never touching the live session's files.
[[nodiscard]] LogResult<> log_crash_init(void);

// Arm the CALLING thread's crash stack: an alternate signal stack (POSIX) or a guaranteed
// stack-overflow handler reservation (win64), so a blown stack on this thread still yields a record.
// thread_create arms every engine thread automatically; only threads created behind the engine's
// back (external libraries, OS callbacks) need to call this themselves. The hooks are process-wide
// and order-independent: arming works before or after log_crash_init. Idempotent per thread.
// Never call on a thread armed by someone else (main is armed by log_crash_init itself).
[[nodiscard]] LogResult<> log_crash_thread_arm(void);

// Release what log_crash_thread_arm reserved, just before the thread exits. Safe to call unarmed.
void log_crash_thread_disarm(void);

} // namespace ano
