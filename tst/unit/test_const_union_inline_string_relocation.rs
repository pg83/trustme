union View { text: &'static str, words: [usize; 2] }
const VALUE: View = View { text: "copper" };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!(value.text, "copper"); }
}
