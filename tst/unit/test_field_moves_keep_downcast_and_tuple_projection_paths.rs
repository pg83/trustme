enum Event { Pair(String, String), Empty }

#[inline(never)]
fn select(event: Event, last: bool) -> String {
    match event {
        Event::Pair(first, second) => {
            let pair = (first, second);
            if last { let selected = pair.1; selected } else { let selected = pair.0; selected }
        }
        Event::Empty => String::new(),
    }
}

fn main() {
    assert_eq!(select(Event::Pair("left".into(), "right".into()), false), "left");
    assert_eq!(select(Event::Pair("left".into(), "right".into()), true), "right");
    assert_eq!(select(Event::Empty, true), "");
}
