// Extracted from library/core/src/clone.rs:25
#[derive(Clone)] // we add the Clone trait to Morpheus struct
struct Morpheus {
   blue_pill: f32,
   red_pill: i64,
}

fn main() {
   let f = Morpheus { blue_pill: 0.0, red_pill: 0 };
   let copy = f.clone(); // and now we can clone it!
}
