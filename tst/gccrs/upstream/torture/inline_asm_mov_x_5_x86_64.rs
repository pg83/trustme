/* { dg-do run { target x86_64*-*-* } } */
/* { dg-output "5\r*\n9\r*\n" }*/

fn gccrs_main() -> i32 {
    let mut value: i32;
    unsafe {
        std::arch::asm!("mov {output:e}, 5", output = out(reg) value);
    }
    println!("{value}");

    let input = 9i32;
    unsafe {
        std::arch::asm!(
            "mov {output:e}, {input:e}",
            input = in(reg) input,
            output = out(reg) value,
        );
    }
    println!("{value}");
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
