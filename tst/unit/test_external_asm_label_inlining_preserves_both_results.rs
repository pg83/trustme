//@ aux-build: external_asm_label_choice.rs
//@ compile-variant: -O

fn main() {
    for value in [0u32, 1, 5] {
        let actual = external_asm_label_choice::select(std::hint::black_box(value));
        assert_eq!(actual, if value == 0 { 109 } else { value + 107 });
    }
}
