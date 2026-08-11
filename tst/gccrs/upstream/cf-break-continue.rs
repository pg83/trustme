// { dg-options "-w" }
// { dg-output "prime\r*\nnot_prime\r*\nprime\r*\nprime\r*\nnot_prime\r*\n" }
fn dump(message: &str) {
    println!("{}", message);
}
fn is_prime(number: i32) -> bool {
    if number <= 1 {
        return false;
    }
    let mut i = 1;
    'prime: loop {
        i += 1;
        if i * i >= number {
            break 'prime;
        }
        if number % i != 0 {
            continue 'prime;
        }
        return false;
    }
    return true;
}

fn debug_prime(number: i32) {
    let state = is_prime(number);
    if state {
        dump("prime");
    } else {
        dump("not_prime");
    }
}

fn gccrs_main() -> i32 {
    debug_prime(11);
    debug_prime(12);
    debug_prime(13);
    debug_prime(17);
    debug_prime(100);
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
