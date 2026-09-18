/* bstr 0.2.17 `BufReadExt::for_byte_record_with_terminator`, which holds combine's
   build: `let mut buf = self.fill_buf()?;` then, in the loop, `buf = rest` and
   `bytes.extend_from_slice(&buf)`.

   Upstream `check_decl_initializer` coerces the initialiser into the local's fresh
   variable, and `Coerce::coerce` (rustc_hir_typeck/src/coercion.rs) with a variable
   target falls through to `unify` - so `buf` is `&[u8]` before anything else is
   checked.  The assignment is not a second opinion: `check_expr_assign`
   (rustc_hir_typeck/src/expr.rs) reads the place's type off the place and only
   demands the value reach it (`demand_coerce`, never `CoerceMany`).

   Reading the two as one `CoerceMany` group left `buf` open while `rest` was owed to
   a method lookup on `buf` itself, and `&buf` into `extend_from_slice`'s `&[?T]`
   then bound `buf` to the parameter's pointee (upstream `coerce_borrowed_pointer`'s
   first autoderef step), giving the local the bare slice `[?T]` and turning the
   `let` into the impossible coercion `[_] := &[u8]`. */
fn fill(s: &[u8]) -> Result<&[u8], ()> {
    Ok(s)
}

fn scan(s: &[u8]) -> Result<Vec<u8>, ()> {
    let mut out = Vec::new();
    let mut buf = fill(s)?;
    let (_head, rest) = buf.split_at(1);
    buf = rest;
    out.extend_from_slice(&buf);
    Ok(out)
}

fn main() {
    assert_eq!(scan(b"abc").unwrap(), b"bc".to_vec());
}
