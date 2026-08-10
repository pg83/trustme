// Extracted from src/generics/new_types.md:10
struct Miles(f64);

struct Kilometers(f64);

impl Miles {
    pub fn to_kilometers(&self) -> Kilometers {
        Kilometers(self.0 * 1.609344)
    }
}

impl Kilometers {
    pub fn to_miles(&self) -> Miles {
        Miles(self.0 / 1.609344)
    }
}

fn is_a_marathon(distance: &Miles) -> bool {
    distance.0 >= 26.2
}

fn main() {
    let distance = Miles(30.0);
    let distance_km = distance.to_kilometers();
    println!("Is a marathon? {}", is_a_marathon(&distance));
    println!("Is a marathon? {}", is_a_marathon(&distance_km.to_miles()));
    // println!("Is a marathon? {}", is_a_marathon(&distance_km));
}
