/* env_logger 0.5 `Logger::log`: `let mut formatter = tl_buf.as_mut().unwrap();` and then
   `(self.format)(&mut formatter, record)`.  `tl_buf` is typed by the coercion of the two
   `if`/`else` arms, so the `as_mut`/`unwrap` lookups are still pending when the argument
   `&mut formatter` is coerced into `&mut Formatter`, and `formatter` is a bare variable.
   Upstream reaches `coerce_borrowed_pointer` with the argument's type as `check_expr`
   produced it - `check_expr_method_call` resolves the receiver first
   (`structurally_resolve_type`), so the call's result is known there - and unifies the
   parameter's pointee with the first autoderef step of the source.  Taking that step for a
   pointee the pending call still has to produce fixes `formatter` at `Formatter`, and
   `unwrap` on `Option<&mut Formatter>`, which yields `&mut Formatter`, then has no
   applicable method.  The real type reaches the parameter by one dereference. */
struct Formatter(u32);

fn take(f: &mut Formatter) {
    f.0 += 1;
}

fn main() {
    let mut a = Some(Formatter(1));
    let mut b = None;
    let tl_buf = if a.is_some() { &mut a } else { &mut b };
    let mut formatter = tl_buf.as_mut().unwrap();
    take(&mut formatter);
    assert_eq!(a.unwrap().0, 2);
}
