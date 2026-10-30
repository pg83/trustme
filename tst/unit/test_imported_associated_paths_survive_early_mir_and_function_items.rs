//@ aux-build: resolved_associated_paths.rs
use resolved_associated_paths::{Feed, Packet};
type Alias = Packet;
const WIDTH: usize = Alias::WIDTH;
const VALUES: [u8; WIDTH] = [7; WIDTH];
fn main() {
    let make: fn() -> Alias = Alias::make;
    let read: fn() -> u16 = resolved_associated_paths::generic_read::<Alias>;
    assert_eq!(make().value(), 17);
    assert_eq!(read(), 17);
    assert_eq!(Alias::inherent().value(), 19);
    assert_eq!(VALUES, [7; 3]);
}
