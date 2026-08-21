// A trait object carries the bounds required to form its principal trait.
// Its surrounding function does not need to repeat those bounds.

trait Required {
    type Assoc;
}

trait Parent<T> {}

trait Child<P: Required>: Parent<P::Assoc> + Parent<()> {}

fn accept<P>(_: &dyn Child<P>) {}

fn main() {}
