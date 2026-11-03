use std::mem::ManuallyDrop;
union Owned<T> { value: ManuallyDrop<T>, empty: () }
struct Frame<T> { slot: Owned<T>, version: u32 }

fn replace<T>(frame: &mut Frame<T>, next: T) -> T {
    let old = unsafe { ManuallyDrop::take(&mut frame.slot.value) };
    frame.slot.value = ManuallyDrop::new(next);
    frame.version += 1;
    old
}

fn main() {
    let mut framed = Box::new(Frame { slot: Owned { value: ManuallyDrop::new(String::from("first")) }, version: 0 });
    assert_eq!(replace(&mut framed, String::from("second")), "first");
    unsafe { (*framed.slot.value).push_str(" value"); }
    assert_eq!(replace(&mut framed, String::from("third")), "second value");
    assert_eq!(framed.version, 2);
    let last = unsafe { ManuallyDrop::take(&mut framed.slot.value) };
    assert_eq!(last, "third");
    let array = [37u16, 41, 43];
    let [first, tail @ ..] = &array[..] else { unreachable!() };
    assert_eq!((*first, tail[1]), (37, 43));
}
