fn main() {
    let value = 0;
    macro_rules! check_definition_site {
        () => {
            assert_eq!(value, 0);
        };
    }

    let value = 1;
    check_definition_site!();
}
