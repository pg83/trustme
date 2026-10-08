// pin-project's tests name associated items through `Self` in the field types
// of an enum's variants: `Tuple([(); Self::ASSOC], ..)`. In a variant, as in
// the rest of an enum's definition, `Self` is the enum. We keep a variant's
// fields in a struct of their own, and the pass that resolves `Self::ASSOC`
// took that struct for `Self`: "Failed to find impl with 'ASSOC' for
// Enum#Tuple".
enum Enum {
    Tuple([(); Self::ASSOC], Box<Self>),
    Struct { f: [(); Self::ASSOC], g: Option<Box<Self>> },
}
impl Enum {
    const ASSOC: usize = 1;
}
fn main() {
    let t = Enum::Tuple([()], Box::new(Enum::Struct { f: [()], g: None }));
    match t {
        Enum::Tuple(a, b) => {
            assert_eq!(a.len(), 1);
            assert!(matches!(*b, Enum::Struct { g: None, .. }));
        }
        Enum::Struct { .. } => unreachable!(),
    }
}
