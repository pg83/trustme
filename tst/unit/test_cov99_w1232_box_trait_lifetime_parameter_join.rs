//@ compile-flags: -Cpanic=abort

trait Readout<'a> {
    fn readout(&self) -> u32;
}

impl<'a> Readout<'a> for u32 {
    fn readout(&self) -> u32 { *self }
}

fn selected_value<'a: 'static>(
    tag: u8,
    left: Box<dyn Readout<'a>>,
    right: Box<dyn Readout<'static>>,
) -> u32 {
    let selected = match tag {
        0 => Box::new(7u32),
        1 => left,
        _ => right,
    };
    selected.readout()
}

fn main() {
    assert_eq!(selected_value(0, Box::new(11u32), Box::new(13u32)), 7);
    assert_eq!(selected_value(1, Box::new(11u32), Box::new(13u32)), 11);
    assert_eq!(selected_value(2, Box::new(11u32), Box::new(13u32)), 13);
}
