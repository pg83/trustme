struct Unit;

fn rebuild(value: Unit) -> Unit {
    Unit { ..value }
}

fn main() {
    let mut calls = 0;
    let _value = rebuild(Unit {
        ..{
            calls += 1;
            Unit
        }
    });
    assert_eq!(calls, 1);
}
