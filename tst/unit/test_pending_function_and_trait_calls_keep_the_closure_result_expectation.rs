trait Read {
    fn read(&self) -> u16;
}

struct Value(u16);

impl Read for Value {
    fn read(&self) -> u16 {
        self.0
    }
}

fn first(make: fn() -> Box<dyn Read>) -> u16 {
    make().read()
}

fn second(make: fn() -> Box<dyn Read>) -> u16 {
    make().read() + 1
}

trait Invoke {
    fn invoke(make: fn() -> Box<dyn Read>, marker: Self) -> u16;
}

struct Marker;

impl Invoke for Marker {
    fn invoke(make: fn() -> Box<dyn Read>, _: Self) -> u16 {
        make().read()
    }
}

fn main() {
    for choose_first in [false, true] {
        let invoke = if choose_first { first } else { second };
        let value = invoke(|| Box::new(Value(223)));
        assert_eq!(value, if choose_first { 223 } else { 224 });
    }
    assert_eq!(Invoke::invoke(|| Box::new(Value(229)), Marker), 229);
}
