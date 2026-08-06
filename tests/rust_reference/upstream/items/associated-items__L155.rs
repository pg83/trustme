// Extracted from src/items/associated-items.md:155
#![allow(unused)]
fn main() {
    type Surface = i32;
    type BoundingBox = i32;
    trait Shape {
        fn draw(&self, surface: Surface);
        fn bounding_box(&self) -> BoundingBox;
    }
}
