// Extracted from src/items/traits.md:237
#![allow(unused)]
fn main() {
    trait Shape { fn area(&self) -> f64; }
    trait Circle: Shape { fn radius(&self) -> f64; }
    fn print_area_and_radius<C: Circle>(c: C) {
        // Here we call the area method from the supertrait `Shape` of `Circle`.
        println!("Area: {}", c.area());
        println!("Radius: {}", c.radius());
    }
}
