//@ compile-fail: #[track_caller] on non-function
#![feature(stmt_expr_attributes)]
fn main() { let _ = #[track_caller] 3u8; }
