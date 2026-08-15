unsafe fn value() -> i32 {
    42
}

fn main() {
    'label: loop {
        break 'label unsafe { value() }
    };

    'block: {
        break 'block 7;
    };
}
