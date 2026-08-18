/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Logging support for tests that link emulator subsystems.
 *
 * src/util/log.cpp implements the gen_log_* emitters, but the verbosity
 * threshold they gate on is owned by the application (src/app/generator.cpp
 * in a real build). A test that pulls in a subsystem which logs therefore
 * needs both: log.cpp for the emitters, and this file for the threshold.
 *
 * Zero silences every level, so a diagnostic taken during a test stays out
 * of the report while still resolving as a real call rather than being
 * compiled away. A test that wants to observe log output can assign to
 * gen_loglevel directly. */

unsigned int gen_loglevel = 0;
