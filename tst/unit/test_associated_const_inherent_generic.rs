// An associated const reached through an inherent impl's path deduces the
// impl's parameters from the type, exactly as a method does. The constant
// branch checked the parameters the path carried instead of the deduced ones
// and asserted with "Mismatch in param counts".
//
// Same shape as rustc's run-pass test consts/issue-58435-ice-with-assoc-const.rs.
struct S<T>(T);

impl<T> S<T> {
    const ID: fn(&S<T>) -> &S<T> = |s| s;

    pub fn id(&self) -> &Self {
        Self::ID(self)
    }
}

fn main() {
    let s = S(10u32);
    assert!(S::<u32>::ID(&s).0 == 10);
    assert!(s.id().0 == 10);
}
