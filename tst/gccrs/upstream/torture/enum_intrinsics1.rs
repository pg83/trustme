/* { dg-output "0\r*\n2\r*\n" } */

#[repr(isize)]
enum BookFormat {
    Paperback,
    Hardback,
    Ebook,
}

fn gccrs_main() -> i32 {
    println!("{}", BookFormat::Paperback as isize);
    println!("{}", BookFormat::Ebook as isize);
    let _ = BookFormat::Hardback;
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
