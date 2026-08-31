fn slice_from_one<'a, T>(slot: &mut &'a mut [T]) -> Option<&'a mut [T]> {
    let value = core::mem::take(slot);
    Some(&mut value[1..])
}

fn main() {}
