// Extracted from src/fn/hof.md:8
fn is_odd(n: u32) -> bool {
    n % 2 == 1
}

fn main() {
    println!("Find the sum of all the numbers with odd squares under 1000");
    let upper = 1000;

    // Imperative approach
    // Declare accumulator variable
    let mut acc = 0;
    // Iterate: 0, 1, 2, ... to infinity
    for n in 0.. {
        // Square the number
        let n_squared = n * n;

        if n_squared >= upper {
            // Break loop if exceeded the upper limit
            break;
        } else if is_odd(n_squared) {
            // Accumulate value, if it's odd
            acc += n;
        }
    }
    println!("imperative style: {}", acc);

    // Functional approach
    let sum: u32 =
        (0..).take_while(|&n| n * n < upper) // Below upper limit
             .filter(|&n| is_odd(n * n))     // That are odd
             .sum();                         // Sum them
    println!("functional style: {}", sum);
}
