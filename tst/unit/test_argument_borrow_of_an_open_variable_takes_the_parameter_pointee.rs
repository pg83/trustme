/* zerocopy-derive's `derive_has_field_struct_union(&ctx.with_input(&variants_union),
   &variants_union.data)`: `variants_union` comes from a generic `parse` and is typed by
   the first argument (`coerce_borrowed_pointer` binds the open pointee to the
   parameter's), so the second unsizes `&Data` to `&dyn DataExt` - it must not fix the
   field's type to `dyn DataExt`. */
trait Parse: Sized {
    fn parse(s: &str) -> Self;
}
struct Data(u32);
struct DeriveInput {
    data: Data,
}
impl Parse for DeriveInput {
    fn parse(s: &str) -> Self {
        DeriveInput { data: Data(s.len() as u32) }
    }
}
fn parse<T: Parse>(s: &str) -> T {
    T::parse(s)
}
trait DataExt {
    fn fields(&self) -> u32;
}
impl DataExt for Data {
    fn fields(&self) -> u32 {
        self.0
    }
}
struct Ctx {
    ast: u32,
}
impl Ctx {
    fn with_input(&self, input: &DeriveInput) -> Self {
        Ctx { ast: self.ast + input.data.0 }
    }
}
fn derive_has_field(ctx: &Ctx, data: &dyn DataExt) -> u32 {
    ctx.ast + data.fields()
}
fn main() {
    let ctx = Ctx { ast: 1 };
    let variants_union = parse("union");
    let out = derive_has_field(&ctx.with_input(&variants_union), &variants_union.data);
    assert_eq!(out, 11);
}
