/* rustc `check_expr_addr_of`: the operand of `&` expects the expected reference's
   pointee, which for an rvalue is only an `rvalue_hint` - an unsized pointee is no
   expectation.  `&id(&1)` against `&&dyn Foo` gives `id` the expected output
   `&dyn Foo`, so its parameter is `&dyn Foo` and `&1` unsizes into it, the literal
   staying an integer; the same for an array's and a tuple's elements. */
trait Mirror {
    type Assoc;
}
impl<T> Mirror for T {
    type Assoc = T;
}

fn id<T>(t: T) -> T {
    t
}

trait Foo {}
impl Foo for i32 {}
impl Foo for u32 {}

fn main() {
    id::<<&&dyn Foo as Mirror>::Assoc>(&id(&1));
    id::<<[Box<dyn Foo>; 2] as Mirror>::Assoc>([Box::new(1i32), Box::new(1u32)]);
    id::<<(Box<dyn Foo>,) as Mirror>::Assoc>((Box::new(1i32),));
}
