//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Failed to find an impl of

trait Projection {
    type Item;
}

trait AProjected<T> {
    fn pick(&self) -> u8;
}

trait ZPlain {
    fn pick(&self) -> u8;
}

trait Routes<P: Projection>: AProjected<<P as Projection>::Item> + ZPlain {}

fn probe<P>() {
    let object: &dyn Routes<P> = loop {};
    let _: u8 = object.pick();
}
