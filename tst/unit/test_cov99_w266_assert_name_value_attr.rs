fn main() {
    assert!({
        #[doc = "name-value separator"]
        let value = true;
        value
    });
}
