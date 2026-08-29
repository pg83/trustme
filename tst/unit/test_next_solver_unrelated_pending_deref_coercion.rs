//@ check-pass

struct Summary {
    median: f64,
    median_abs_dev: f64,
    median_abs_dev_pct: f64,
}

fn summarize(samples: &[f64]) -> Summary {
    Summary {
        median: samples[0],
        median_abs_dev: 1.0,
        median_abs_dev_pct: 0.5,
    }
}

fn main() {
    let samples: &mut [f64] = &mut [0.0; 4];
    for value in &mut *samples {
        *value = 2.0;
    }
    let first = summarize(samples);
    for value in &mut *samples {
        *value = 1.5;
    }
    let second = summarize(samples);

    assert!(first.median_abs_dev_pct < 1.0);
    assert!(first.median - second.median < second.median_abs_dev);
}
