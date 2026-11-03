//@ compile-fail: Type mismatch

trait Project { type Output; }

fn make<T: Project>() -> <T as Project>::Output {
    17
}

fn main() {}
