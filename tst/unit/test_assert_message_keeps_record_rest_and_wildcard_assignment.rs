//@ compile-variant: -O
struct Pair { left: u16, right: u16 }
fn main() {
    std::panic::set_hook(Box::new(|info| {
        let text = info.to_string();
        let compact: String = text.split_whitespace().collect();
        if compact.contains("Pair{left,..}") && compact.contains("_=31") {
            std::process::exit(0);
        }
        eprintln!("assertion message lost a rest or wildcard assignment: {text}");
        std::process::exit(17);
    }));
    let mut left = 0;
    assert!({
        Pair { left, .. } = Pair { left: 17, right: 23 };
        _ = 31;
        left != 17
    });
    std::process::exit(19);
}
