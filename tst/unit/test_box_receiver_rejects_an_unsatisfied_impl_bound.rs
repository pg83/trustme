//@ compile-fail: No applicable methods
struct Unclonable;
struct Parcel<T>(T);
impl<T: Clone> Parcel<T> {
    fn inspect(self: Box<Self>) -> u32 { 41 }
}
fn main() {
    let _ = Box::new(Parcel(Unclonable)).inspect();
}
