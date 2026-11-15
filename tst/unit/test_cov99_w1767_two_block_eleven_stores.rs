//@ crate-type: lib
//@ compile-flags: -O -Z dump-mir --emit=metadata

pub type Snapshot = (u32, u32, u32, u32, u32, u32, u32, u32, u32, u32, u32);

fn a_invoke(callback: fn() -> u32) -> u32 {
    callback()
}

fn b_repeat(callback: fn() -> u32, snapshot: &mut Snapshot, mut _last: u32) -> ! {
    snapshot.0 = 1;
    snapshot.1 = 2;
    snapshot.2 = 3;
    snapshot.3 = 4;
    snapshot.4 = 5;
    snapshot.5 = 6;
    snapshot.6 = 7;
    snapshot.7 = 8;
    snapshot.8 = 9;
    snapshot.9 = 10;
    snapshot.10 = 11;
    _last = a_invoke(callback);
    loop {
        snapshot.0 = 1;
        snapshot.1 = 2;
        snapshot.2 = 3;
        snapshot.3 = 4;
        snapshot.4 = 5;
        snapshot.5 = 6;
        snapshot.6 = 7;
        snapshot.7 = 8;
        snapshot.8 = 9;
        snapshot.9 = 10;
        snapshot.10 = 11;
        _last = a_invoke(callback);
    }
}

pub fn repeat_updates(callback: fn() -> u32, snapshot: &mut Snapshot, last: u32) -> ! {
    b_repeat(callback, snapshot, last)
}
