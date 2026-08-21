fn main() {
    let address;
    match 0 {
        ref mut value if {
            address = &value as *const _;
            true
        } => assert_eq!(address, &value as *const _),
        _ => unreachable!(),
    }
}
