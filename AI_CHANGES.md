# AI Changes

This file tracks changes made to this repository by an AI coding assistant.

## Model

Claude Sonnet 5 (`claude-sonnet-5`), via Claude Code.

## Changes

### 2026-07-25 — Fix missing `LEX::cleanup_lex_after_parse_error()` call

**File:** `statement_digest.cc` (`parse_for_digest()`)

**Context:** A full code review of the plugin was performed, cross-checking
its use of internal MariaDB server APIs against the actual server source
(`sql_parse.cc`, `sql_lex.cc`, `sp_head.cc`, `sql_class.h`, `sql_digest.h`,
`sql_digest_stream.h`, `plugin_function.h`). The review found that
`parse_for_digest()` invokes `MYSQLparse()`/`ORAparse()` directly instead of
going through the server's `parse_sql()` wrapper.

`parse_sql()` (`sql/sql_parse.cc`) always calls
`LEX::cleanup_lex_after_parse_error(thd)` after a failed parse. That cleanup
undoes `sp_head::reset_thd_mem_root()`, which the parser invokes while
parsing the body of a `CREATE PROCEDURE`/`FUNCTION`/`TRIGGER`/`EVENT` or
package, and which temporarily repoints `thd->mem_root`/`thd->free_list` at
the routine's own memory root.

Without this call, a syntax error inside such a routine body (e.g.
`STATEMENT_DIGEST('CREATE PROCEDURE p() BEGIN GARBAGE END')`) left
`thd->mem_root` pointing at the `sp_head`'s memory root when `MYSQLparse()`
returned. The plugin's `Digest_parse_context` destructor then calls
`lex_end(&m_lex)`, which frees that `sp_head` (`sp_head::destroy()` →
`free_root()`), leaving `thd->mem_root` briefly dangling until the
subsequent `restore_active_arena()` call happens to overwrite it. This is a
real deviation from a documented, mandatory cleanup step and a latent
use-after-free risk, even though no allocation currently occurs in that
window.

**Fix:** call `LEX::cleanup_lex_after_parse_error(thd)` in
`parse_for_digest()` whenever `MYSQLparse()`/`ORAparse()` returns a parse
error, before returning to the caller — matching the behavior of
`parse_sql()`.
