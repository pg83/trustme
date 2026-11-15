struct Unit;

struct Wrapper {
    value: Unit,
}

fn main() {
    let unit = Unit;
    let mut wrapper = Wrapper { value: Unit };
    loop {
        let Unit {} = unit;
        let Wrapper { value: Unit {} } = wrapper;
        wrapper.value = Unit;
        break;
    }
}
