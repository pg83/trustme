fn main() {
    #[cfg(any())]
    compile_error!("cfg-disabled expression macro was expanded");
}
