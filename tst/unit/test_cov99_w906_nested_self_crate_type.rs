extern crate self as local_crate;

pub struct Marker;

mod consumer {
    use local_crate::Marker;

    pub fn make() -> Marker {
        Marker
    }
}

fn main() {
    let _: Marker = consumer::make();
}
