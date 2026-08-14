struct Holder<'a> {
    value: &'a usize,
}

fn assign_unreachable_rhs() {
    let value = 5;
    let mut holder = Holder { value: &value };
    holder.value = &panic!();
}

fn main() {}
