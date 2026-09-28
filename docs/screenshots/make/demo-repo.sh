#!/usr/bin/env bash
# Builds "tidewatch", the fictional repository the README pictures show.
#   demo-repo.sh <home>  →  <home>/Projects/tidewatch (+ its origin in <home>/.remotes)
# Fixed names, e-mails and dates, so every run gives the same commits: three lines of
# work after v0.2.0, merged branches below them, one commit not pushed yet, and a
# change in progress (five files for the commit, one test left out).
set -euo pipefail
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1

home=$1
repo=$home/Projects/tidewatch
origin=$home/.remotes/tidewatch.git
rm -rf "$repo" "$origin"
mkdir -p "$repo" "$(dirname "$origin")"
git init -q --bare -b main "$origin"
cd "$repo"
git init -q -b main
git config user.name "Mira Okafor"
git config user.email "mira@example.com"
git config commit.gpgsign false
git remote add origin "$origin"

declare -A NAME=([mira]="Mira Okafor" [jonas]="Jonas Lindqvist" [priya]="Priya Raman")

commit() { # who, date, subject[, body]
    git add -A
    GIT_AUTHOR_NAME=${NAME[$1]} GIT_AUTHOR_EMAIL=$1@example.com \
    GIT_COMMITTER_NAME=${NAME[$1]} GIT_COMMITTER_EMAIL=$1@example.com \
    GIT_AUTHOR_DATE="$2 +0200" GIT_COMMITTER_DATE="$2 +0200" \
        git commit -q -m "$3" ${4:+-m "$4"}
}

merge() { # who, date, branch
    GIT_AUTHOR_NAME=${NAME[$1]} GIT_AUTHOR_EMAIL=$1@example.com \
    GIT_COMMITTER_NAME=${NAME[$1]} GIT_COMMITTER_EMAIL=$1@example.com \
    GIT_AUTHOR_DATE="$2 +0200" GIT_COMMITTER_DATE="$2 +0200" \
        git merge -q --no-ff --no-edit "$3"
}

# ── v0.1 ────────────────────────────────────────────────────────────────────

mkdir -p src tests
cat > .gitignore <<'EOF'
/target
EOF
cat > Cargo.toml <<'EOF'
[package]
name = "tidewatch"
version = "0.1.0"
edition = "2024"
description = "Tide times in your terminal"
license = "MIT"

[dependencies]
anyhow = "1"
chrono = "0.4"
EOF
cat > src/main.rs <<'EOF'
mod forecast;

fn main() {
    let table = forecast::sample();
    for r in &table.readings {
        println!("{}  {:.2} m", r.at, r.height);
    }
}
EOF
cat > src/forecast.rs <<'EOF'
use chrono::{DateTime, Utc};

#[derive(Debug, Clone, Copy)]
pub struct Reading {
    pub at: DateTime<Utc>,
    pub height: f32,
}

pub struct Table {
    pub readings: Vec<Reading>,
}

pub fn sample() -> Table {
    Table { readings: Vec::new() }
}
EOF
cat > README.md <<'EOF'
# tidewatch

Tide times in your terminal.
EOF
commit mira "2026-09-06 10:02" "Start tidewatch with one hard-coded station"

mkdir -p .github/workflows
cat > .github/workflows/ci.yml <<'EOF'
name: CI
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cargo clippy -- -D warnings
      - run: cargo test
EOF
commit mira "2026-09-06 15:40" "Run cargo test and clippy in CI"

cat > rustfmt.toml <<'EOF'
max_width = 80
use_small_heuristics = "Max"
EOF
commit jonas "2026-09-07 11:15" "Add rustfmt settings"

cat > src/forecast.rs <<'EOF'
use anyhow::Result;
use chrono::{DateTime, Utc};
use serde::Deserialize;

#[derive(Debug, Clone, Copy, Deserialize)]
pub struct Reading {
    pub at: DateTime<Utc>,
    pub height: f32,
}

pub struct Table {
    pub readings: Vec<Reading>,
}

/// Reads a tide table: one `time,height` row
/// per reading, oldest first.
pub fn parse(csv: &str) -> Result<Table> {
    let mut rows = csv::Reader::from_reader(csv.as_bytes());
    let readings = rows
        .deserialize()
        .collect::<Result<Vec<Reading>, _>>()?;
    Ok(Table { readings })
}
EOF
sed -i 's/^chrono = "0.4"$/chrono = { version = "0.4", features = ["serde"] }\ncsv = "1.3"\nserde = { version = "1", features = ["derive"] }/' Cargo.toml
commit mira "2026-09-08 17:20" "Parse tide tables from CSV"

cat >> src/forecast.rs <<'EOF'

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Tide {
    High,
    Low,
}

impl Table {
    /// High and low water, in time order.
    pub fn turns(&self) -> Vec<(Tide, Reading)> {
        self.readings
            .windows(3)
            .filter_map(|w| {
                let (a, b, c) = (w[0], w[1], w[2]);
                if b.height > a.height && b.height >= c.height {
                    Some((Tide::High, b))
                } else if b.height < a.height && b.height <= c.height {
                    Some((Tide::Low, b))
                } else {
                    None
                }
            })
            .collect()
    }
}
EOF
cat > src/render.rs <<'EOF'
use crate::forecast::{Table, Tide};
use chrono::Local;

pub fn print(name: &str, table: &Table) {
    println!("{name}");
    let today = Local::now().date_naive();
    for (tide, r) in table.turns() {
        let at = r.at.with_timezone(&Local);
        if at.date_naive() != today {
            continue;
        }
        let label = match tide {
            Tide::High => "High",
            Tide::Low => "Low ",
        };
        println!(
            "  {label}  {}  {:>5.2} m",
            at.format("%H:%M"),
            r.height,
        );
    }
}
EOF
cat > src/main.rs <<'EOF'
mod forecast;
mod render;

use anyhow::Result;

const STATION: &str = "Brest";
const TABLE: &str = include_str!("../data/brest.csv");

fn main() -> Result<()> {
    let table = forecast::parse(TABLE)?;
    render::print(STATION, &table);
    Ok(())
}
EOF
mkdir -p data
printf 'at,height\n2026-09-15T00:10:00Z,1.92\n2026-09-15T06:25:00Z,6.41\n2026-09-15T12:38:00Z,1.74\n2026-09-15T18:52:00Z,6.58\n' > data/brest.csv
commit mira "2026-09-09 09:45" "Print high and low water for today"

git switch -q -c ci-cache
python3 - <<'EOF'
p = ".github/workflows/ci.yml"
s = open(p).read()
s = s.replace("      - uses: actions/checkout@v4\n", "      - uses: actions/checkout@v4\n      - uses: Swatinem/rust-cache@v2\n")
open(p, "w").write(s)
EOF
commit jonas "2026-09-10 10:20" "Cache cargo builds in CI"
python3 - <<'EOF'
p = ".github/workflows/ci.yml"
s = open(p).read()
s = s.replace("    runs-on: ubuntu-latest\n", "    runs-on: ubuntu-latest\n    strategy:\n      matrix:\n        rust: [stable, beta]\n")
s = s.replace("      - uses: Swatinem/rust-cache@v2\n", "      - uses: dtolnay/rust-toolchain@master\n        with:\n          toolchain: ${{ matrix.rust }}\n      - uses: Swatinem/rust-cache@v2\n")
open(p, "w").write(s)
EOF
commit jonas "2026-09-11 16:05" "Test on stable and beta"
git switch -q main

cat >> README.md <<'EOF'

Tide tables come from the national hydrographic offices,
cached for a day.
EOF
commit priya "2026-09-10 14:12" "Say where the tide tables come from"
merge mira "2026-09-11 18:30" ci-cache
git branch -q -D ci-cache

cat > src/config.rs <<'EOF'
use anyhow::Result;
use serde::Deserialize;
use std::path::PathBuf;

#[derive(Debug, Default, Deserialize)]
#[serde(default)]
pub struct Config {
    /// Station shown when none is named
    pub home: Option<String>,
    /// Metres or feet
    pub units: Units,
}

#[derive(Debug, Default, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Units {
    #[default]
    Metres,
    Feet,
}

pub fn load() -> Result<Config> {
    let path = path();
    if !path.exists() {
        return Ok(Config::default());
    }
    let text = std::fs::read_to_string(path)?;
    Ok(toml::from_str(&text)?)
}

fn path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_default()
        .join("tidewatch/config.toml")
}
EOF
printf 'dirs = "6"\ntoml = "0.9"\n' >> Cargo.toml
sed -i 's/^mod forecast;$/mod config;\nmod forecast;/' src/main.rs
commit jonas "2026-09-12 11:30" "Add a config file for the home station"

mkdir -p doc
cat > doc/tidewatch.1 <<'EOF'
.TH TIDEWATCH 1
.SH NAME
tidewatch \- tide times in your terminal
.SH SYNOPSIS
.B tidewatch
[\fIstation\fR]
EOF
commit priya "2026-09-14 15:48" "Add a man page"

cat >> README.md <<'EOF'

```bash
cargo install tidewatch
tidewatch brest
```
EOF
commit jonas "2026-09-17 10:05" "Release 0.1.0"
git tag v0.1.0

# ── station search (Priya) while Jonas adds the cache ───────────────────────

git switch -q -c station-search
cat > src/station.rs <<'EOF'
use anyhow::{Result, bail};
use chrono_tz::Tz;

pub struct Station {
    pub id: &'static str,
    pub name: &'static str,
    pub tz: Tz,
}

pub const ALL: &[Station] = &[
    Station { id: "FR-BRE", name: "Brest", tz: Tz::Europe__Paris },
    Station { id: "GB-DOV", name: "Dover", tz: Tz::Europe__London },
    Station { id: "NL-HVH", name: "Hoek van Holland", tz: Tz::Europe__Amsterdam },
    Station { id: "PT-LIS", name: "Lisboa", tz: Tz::Europe__Lisbon },
];

pub fn find(name: &str) -> Result<&'static Station> {
    match ALL.iter().find(|s| s.name.eq_ignore_ascii_case(name)) {
        Some(s) => Ok(s),
        None => bail!("no station called {name}"),
    }
}
EOF
printf 'chrono-tz = "0.10"\n' >> Cargo.toml
commit priya "2026-09-18 15:34" "Look up stations by name"

cat > src/station.rs.new <<'EOF'
use anyhow::{Result, bail};
use chrono_tz::Tz;
use strsim::jaro_winkler;

pub struct Station {
    pub id: &'static str,
    pub name: &'static str,
    pub tz: Tz,
}

pub const ALL: &[Station] = &[
    Station { id: "FR-BRE", name: "Brest", tz: Tz::Europe__Paris },
    Station { id: "GB-DOV", name: "Dover", tz: Tz::Europe__London },
    Station { id: "NL-HVH", name: "Hoek van Holland", tz: Tz::Europe__Amsterdam },
    Station { id: "PT-LIS", name: "Lisboa", tz: Tz::Europe__Lisbon },
];

/// The station whose name is closest to `name`,
/// if any comes close enough.
pub fn find(name: &str) -> Result<&'static Station> {
    let best = ALL
        .iter()
        .map(|s| (jaro_winkler(&s.name.to_lowercase(), &name.to_lowercase()), s))
        .max_by(|a, b| a.0.total_cmp(&b.0));
    match best {
        Some((score, s)) if score > 0.85 => Ok(s),
        _ => bail!("no station called {name}"),
    }
}
EOF
mv src/station.rs.new src/station.rs
printf 'strsim = "0.11"\n' >> Cargo.toml
commit priya "2026-09-20 16:47" "Fuzzy-match station names"

cat >> src/station.rs <<'EOF'

/// The three names closest to `name`, for the
/// error message when nothing matched.
pub fn suggestions(name: &str) -> Vec<&'static str> {
    let mut all: Vec<_> = ALL
        .iter()
        .map(|s| (jaro_winkler(s.name, name), s.name))
        .collect();
    all.sort_by(|a, b| b.0.total_cmp(&a.0));
    all.into_iter().take(3).map(|(_, n)| n).collect()
}
EOF
commit priya "2026-09-22 10:33" "Show the three nearest matches when nothing fits"

git switch -q main
cat > src/cache.rs <<'EOF'
use anyhow::Result;
use std::path::PathBuf;
use std::time::{Duration, SystemTime};

const MAX_AGE: Duration = Duration::from_secs(24 * 60 * 60);

/// A downloaded table younger than a day.
pub fn fresh(id: &str) -> Option<String> {
    let path = path(id);
    let age = path.metadata().ok()?.modified().ok()?;
    if SystemTime::now().duration_since(age).ok()? > MAX_AGE {
        return None;
    }
    std::fs::read_to_string(path).ok()
}

pub fn store(id: &str, csv: &str) -> Result<()> {
    std::fs::write(path(id), csv)?;
    Ok(())
}

fn path(id: &str) -> PathBuf {
    dirs::cache_dir()
        .unwrap_or_default()
        .join(format!("tidewatch/{id}.csv"))
}
EOF
commit jonas "2026-09-19 11:12" "Cache downloaded tables for a day"

sed -i 's|^    std::fs::write(path(id), csv)?;$|    let path = path(id);\n    if let Some(dir) = path.parent() {\n        std::fs::create_dir_all(dir)?;\n    }\n    std::fs::write(path, csv)?;|' src/cache.rs
commit jonas "2026-09-21 14:03" "Fix the cache path on first run" \
    "The cache directory does not exist yet the first time tidewatch runs."

merge mira "2026-09-22 17:01" station-search

# ── UTC offsets (Jonas) while Mira colours the output ──────────────────────

git switch -q -c utc-offsets
sed -i 's/^use chrono::Local;$/use crate::station::Station;/; s/^pub fn print(name: \&str, table: \&Table) {$/pub fn print(station: \&Station, table: \&Table) {/; s/^    println!("{name}");$/    println!("{}", station.name);/; s/^    let today = Local::now().date_naive();$/    let today = chrono::Utc::now().with_timezone(\&station.tz).date_naive();/; s/^        let at = r.at.with_timezone(&Local);$/        let at = r.at.with_timezone(\&station.tz);/' src/render.rs
commit jonas "2026-09-23 15:22" "Convert times to the station's time zone"

cat >> src/station.rs <<'EOF'

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn east_of_the_date_line() {
        let s = find("Nuku'alofa").unwrap();
        assert_eq!(s.tz, Tz::Pacific__Tongatapu);
    }
}
EOF
sed -i 's|^    Station { id: "PT-LIS", name: "Lisboa", tz: Tz::Europe__Lisbon },$|    Station { id: "PT-LIS", name: "Lisboa", tz: Tz::Europe__Lisbon },\n    Station { id: "TO-NUK", name: "Nuku'"'"'alofa", tz: Tz::Pacific__Tongatapu },|' src/station.rs
commit jonas "2026-09-24 11:04" "Handle stations east of the date line"

git switch -q main
printf 'owo-colors = "4"\n' >> Cargo.toml
sed -i 's/^use crate::forecast::{Table, Tide};$/use crate::forecast::{Table, Tide};\nuse owo_colors::OwoColorize;/; s/^            Tide::High => "High",$/            Tide::High => "High".blue().to_string(),/; s/^            Tide::Low => "Low ",$/            Tide::Low => "Low ".yellow().to_string(),/' src/render.rs
commit mira "2026-09-24 16:12" "Colour rising and falling water"

merge mira "2026-09-25 14:31" utc-offsets >/dev/null 2>&1 || true   # render.rs conflicts
if [ -e .git/MERGE_HEAD ]; then
    git checkout -q --theirs src/render.rs
    sed -i 's/^use crate::station::Station;$/use crate::station::Station;\nuse owo_colors::OwoColorize;/; s/^            Tide::High => "High",$/            Tide::High => "High".blue().to_string(),/; s/^            Tide::Low => "Low ",$/            Tide::Low => "Low ".yellow().to_string(),/' src/render.rs
    git add -A
    GIT_AUTHOR_NAME="Mira Okafor" GIT_AUTHOR_EMAIL=mira@example.com \
    GIT_COMMITTER_NAME="Mira Okafor" GIT_COMMITTER_EMAIL=mira@example.com \
    GIT_AUTHOR_DATE="2026-09-25 14:31 +0200" GIT_COMMITTER_DATE="2026-09-25 14:31 +0200" \
        git commit -q --no-edit
fi

sed -i 's/^version = "0.1.0"$/version = "0.2.0"/' Cargo.toml
commit jonas "2026-09-25 15:02" "Release 0.2.0"
git tag v0.2.0
git branch -q -D station-search utc-offsets

# ── three lines of work after 0.2.0 ─────────────────────────────────────────

cat > tests/forecast.rs <<'EOF'
use tidewatch::forecast::{Tide, parse};

const DAY: &str = "at,height
2026-09-15T00:10:00Z,1.92
2026-09-15T06:25:00Z,6.41
2026-09-15T12:38:00Z,1.74
2026-09-15T18:52:00Z,6.58
";

#[test]
fn finds_high_and_low_water() {
    let table = parse(DAY).unwrap();
    let turns: Vec<_> = table.turns().iter().map(|t| t.0).collect();
    assert_eq!(turns, [Tide::High, Tide::Low]);
}
EOF
mkdir -p docs
cat > docs/config.md <<'EOF'
# Configuration

`~/.config/tidewatch/config.toml`:

```toml
home = "brest"    # the station shown when none is named
units = "metres"  # or "feet"
```
EOF
commit jonas "2026-09-26 10:17" "Document the config keys"

git switch -q -c moon-phase
cat > src/moon.rs <<'EOF'
use chrono::{DateTime, Utc};

/// Days since the new moon, 0.0 to 29.53.
pub fn age(at: DateTime<Utc>) -> f64 {
    const SYNODIC: f64 = 29.530_588;
    let known_new = 947_182_440.0; // 2000-01-06 18:14 UTC
    let days = (at.timestamp() as f64 - known_new) / 86_400.0;
    days.rem_euclid(SYNODIC)
}

/// Spring tides follow new and full moon.
pub fn is_spring(at: DateTime<Utc>) -> bool {
    let a = age(at);
    a < 2.0 || (a - 14.77).abs() < 2.0 || a > 27.5
}
EOF
commit priya "2026-09-26 17:33" "Show the moon phase next to spring tides"

git switch -q main
sed -i 's|^    dirs::cache_dir()$|    std::env::var_os("XDG_CACHE_HOME")\n        .map(PathBuf::from)\n        .or_else(dirs::cache_dir)|' src/cache.rs
commit priya "2026-09-27 16:08" "Keep the cache under \$XDG_CACHE_HOME"

git switch -q moon-phase
cat >> src/moon.rs <<'EOF'

/// A spring tide with the moon near perigee.
pub fn is_king(at: DateTime<Utc>, perigee_km: f64) -> bool {
    is_spring(at) && perigee_km < 360_000.0
}
EOF
commit priya "2026-09-27 18:42" "Mark king tides in the forecast"

git switch -q -c hourly-chart v0.2.0   # branch off right after the release

python3 - <<'EOF'
p = "src/forecast.rs"
s = open(p).read()
s = s.replace("use chrono::{DateTime, Utc};", "use chrono::{DateTime, Duration, Utc};")
s = s.rstrip("\n")
assert s.endswith("}")
s = s[:-1] + '''
    /// The water level every `step` from `start`
    /// for `span`, between the readings.
    pub fn samples(
        &self,
        start: DateTime<Utc>,
        span: Duration,
        step: Duration,
    ) -> Vec<f32> {
        let mut out = Vec::new();
        let mut t = start;
        while t < start + span {
            out.push(self.height_at(t));
            t += step;
        }
        out
    }

    fn height_at(&self, t: DateTime<Utc>) -> f32 {
        let i = self.readings.partition_point(|r| r.at <= t);
        let (a, b) = match i {
            0 => return self.readings[0].height,
            n if n == self.readings.len() => {
                return self.readings[n - 1].height;
            }
            n => (self.readings[n - 1], self.readings[n]),
        };
        let span = (b.at - a.at).num_seconds() as f32;
        let part = (t - a.at).num_seconds() as f32;
        a.height + (b.height - a.height) * part / span
    }
}
'''
open(p, "w").write(s)
EOF
commit mira "2026-09-27 11:24" "Sample the water level every ten minutes"

python3 - <<'EOF'
p = "src/render.rs"
s = open(p).read()
s = s.replace("use crate::forecast::{Table, Tide};\n",
              "use crate::forecast::{Table, Tide};\nuse chrono::{Duration, Utc};\n")
s = s.replace('''    println!("{}", station.name);
''', '''    println!("{}", station.name.bold());
    let samples = table.samples(
        Utc::now(),
        Duration::hours(12),
        Duration::minutes(10),
    );
    println!("  {}", sparkline(&samples).cyan());
''')
s += '''
const BARS: [char; 8] = ['▁', '▂', '▃', '▄', '▅', '▆', '▇', '█'];

/// One bar per sample, scaled between the
/// lowest and highest water in view.
pub fn sparkline(samples: &[f32]) -> String {
    let lo = samples.iter().copied().fold(f32::MAX, f32::min);
    let hi = samples.iter().copied().fold(f32::MIN, f32::max);
    let range = (hi - lo).max(f32::EPSILON);
    samples
        .iter()
        .map(|h| BARS[((h - lo) / range * 7.0).round() as usize])
        .collect()
}
'''
open(p, "w").write(s)
EOF
commit mira "2026-09-28 09:12" "Draw the next twelve hours as a sparkline" \
    "One bar every ten minutes, scaled between the lowest and highest water in view, so the turn of the tide is easy to spot."

# ── publish: origin has main, moon-phase and the first chart commit ─────────

git push -q origin main moon-phase v0.1.0 v0.2.0
git push -q origin "hourly-chart~1:refs/heads/hourly-chart"
git branch -q -D moon-phase
git fetch -q origin
git branch -q -u origin/hourly-chart hourly-chart

# ── work in progress: move the chart into its own module ────────────────────

python3 - <<'EOF'
import re
p = "src/render.rs"
s = open(p).read()
s = s.replace("use crate::forecast::{Table, Tide};\nuse chrono::{Duration, Utc};\n",
              "use crate::chart;\nuse crate::config::Config;\nuse crate::forecast::{Table, Tide};\nuse chrono::Utc;\n")
s = s.replace("pub fn print(station: &Station, table: &Table) {",
              "pub fn print(station: &Station, table: &Table, config: &Config) {")
s = s.replace('''    let samples = table.samples(
        Utc::now(),
        Duration::hours(12),
        Duration::minutes(10),
    );
    println!("  {}", sparkline(&samples).cyan());
''', '''    if config.chart_hours > 0 {
        let hours = config.chart_hours.into();
        println!("{}", chart::draw(table, Utc::now(), hours));
    }
''')
s = s[:s.index("\nconst BARS")] + "\n"
open(p, "w").write(s)

p = "src/forecast.rs"
s = open(p).read()
s = s.replace('''    fn height_at(''', '''    /// The lowest and the highest reading.
    pub fn range(&self) -> (f32, f32) {
        self.readings.iter().fold(
            (f32::MAX, f32::MIN),
            |(lo, hi), r| (lo.min(r.height), hi.max(r.height)),
        )
    }

    fn height_at(''')
open(p, "w").write(s)

p = "src/config.rs"
s = open(p).read()
s = s.replace('''#[derive(Debug, Default, Deserialize)]
#[serde(default)]
pub struct Config {''', '''#[derive(Debug, Deserialize)]
#[serde(default)]
pub struct Config {''')
s = s.replace('''    /// Metres or feet
    pub units: Units,
}
''', '''    /// Metres or feet
    pub units: Units,
    /// Hours in the chart, 0 hides it
    pub chart_hours: u8,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            home: None,
            units: Units::Metres,
            chart_hours: 12,
        }
    }
}
''')
open(p, "w").write(s)

p = "README.md"
s = open(p).read()
s = s.replace('''units = "metres"  # or "feet"
''', '''units = "metres"  # or "feet"
chart_hours = 12  # hours in the chart, 0 hides it
''')
s += '''
## Chart

Under the times, a bar chart shows the water for the next
`chart_hours`, with a mark under every third hour.
'''
open(p, "w").write(s)
EOF

cat > src/chart.rs <<'EOF'
use crate::forecast::Table;
use chrono::{DateTime, Duration, Local, Timelike, Utc};
use owo_colors::OwoColorize;

const BARS: [char; 8] = ['▁', '▂', '▃', '▄', '▅', '▆', '▇', '█'];
const STEP: i64 = 20; // minutes per bar

/// The next `hours` of water as bars, with the
/// hour under every third hour's bar.
pub fn draw(table: &Table, from: DateTime<Utc>, hours: i64) -> String {
    let step = Duration::minutes(STEP);
    let samples = table.samples(from, Duration::hours(hours), step);
    let (lo, hi) = table.range();
    let mut bars = String::new();
    let mut marks = String::new();
    for (i, h) in samples.iter().enumerate() {
        let level = ((h - lo) / (hi - lo) * 7.0).round() as usize;
        bars.push(BARS[level.min(7)]);
        let at = (from + step * i as i32).with_timezone(&Local);
        if at.minute() < STEP as u32 && at.hour() % 3 == 0 {
            marks.push_str(&format!("{:<2}", at.hour()));
        } else if marks.chars().count() <= i {
            marks.push(' ');
        }
    }
    format!("  {}\n  {}", bars.cyan(), marks.dimmed())
}
EOF
git add src/chart.rs

mkdir -p tests
cat > tests/chart.rs <<'EOF'
use chrono::{TimeZone, Utc};
use tidewatch::{chart, forecast::parse};

#[test]
fn one_bar_per_twenty_minutes() {
    let table = parse(include_str!("../data/brest.csv")).unwrap();
    let from = Utc.with_ymd_and_hms(2026, 9, 15, 6, 0, 0).unwrap();
    let out = chart::draw(&table, from, 12);
    let bars = out.lines().next().unwrap().trim();
    assert_eq!(bars.chars().count(), 36);
}
EOF

