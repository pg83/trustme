#![feature(rustc_attrs)]
#![rustc_no_implicit_bounds]

trait Marker {}

fn accept<T: Marker>() {}

fn main() {
    accept::<dyn Marker>();
}
