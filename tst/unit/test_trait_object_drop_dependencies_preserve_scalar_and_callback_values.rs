use std::cell::Cell;
use std::rc::Rc;

trait Value {
    fn value(&self) -> u16;
}

struct Tracked {
    value: u16,
    drops: Rc<Cell<u16>>,
}

impl Value for Tracked {
    fn value(&self) -> u16 {
        self.value
    }
}

impl Drop for Tracked {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + self.value);
    }
}

fn adjust(value: u16) -> u16 {
    value + 7
}

fn main() {
    let drops = Rc::new(Cell::new(0u16));
    {
        let object: Box<dyn Value> = Box::new(Tracked { value: 17, drops: drops.clone() });
        let pointer: *const dyn Value = &*object;
        let callback: fn(u16) -> u16 = adjust;
        assert_eq!(unsafe { (*pointer).value() }, 17);
        assert_eq!(std::hint::black_box(callback)(object.value()), 24);
        assert_eq!(drops.get(), 0);
    }
    assert_eq!(drops.get(), 17);
}
