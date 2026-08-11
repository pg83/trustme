enum BookFormat {
    Paperback,
    Hardback,
    Ebook,
}

fn gccrs_main() -> i32 {
    let variants = [
        BookFormat::Paperback,
        BookFormat::Hardback,
        BookFormat::Ebook,
    ];
    variants.len() as i32 - 3
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
