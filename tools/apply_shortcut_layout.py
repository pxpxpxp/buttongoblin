"""Apply the approved physical page-two layout to the recovered v1 package."""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
app = root / "Scripts/.config/buttongoblin"
order = [3, 0, 1, 0, 9, 0, 5, 6, 7, 4, 8, 0, 0, 0, 14]
conf = app / "buttons-shortcuts.conf"
text = conf.read_text()
blocks = re.findall(r"key\.(\d+)\.label=.*?(?=key\.\d+\.label=|\Z)", text, re.S)
assert len(blocks) == 15
entries = re.findall(r"key\.\d+\.label=.*?(?=key\.\d+\.label=|\Z)", text, re.S)
images = [(app / "icons/custom" / f"page2_{i:02d}.jpg").read_bytes() for i in range(15)]
head = text[:text.index("key.0.label")]
result = []
for new, old in enumerate(order):
    entry = re.sub(r"key\.\d+\.", f"key.{new}.", entries[old])
    entry = re.sub(r"custom/page2_\d+\.jpg", f"custom/page2_{new:02d}.jpg", entry)
    result.append(entry.rstrip() + "\n")
    (app / "icons/custom" / f"page2_{new:02d}.jpg").write_bytes(images[old])
conf.write_text(head + "".join(result))
expected = {0:"menu",2:"up",4:"escape",6:"left",7:"down",8:"right",9:"enter",10:"space",14:"back"}
for i in range(15):
    assert f"key.{i}.action={expected.get(i,'none')}\n" in conf.read_text()
old = "2 Up; 4 Menu; 5 Enter; 6 Left; 7 Down; 8 Right; 9 Space; 10 Escape; 15 Back."
new = "1 Menu; 3 Up; 5 Escape; 7 Left; 8 Down; 9 Right; 10 Enter; 11 Space; 15 Back."
for name in ("TWO_PAGES.txt", "README.txt"):
    p = root / name
    p.write_text(p.read_text().replace(old, new))
with (root / "CHANGES.txt").open("a") as f:
    f.write("\nPage two: centered arrows (Up 3; Left/Down/Right 7/8/9), Menu 1, Escape 5, Enter 10, Space 11, Back 15. Guide and editor defaults match.\n")
print("PASS: approved shortcut actions and artwork positions")

p = root / "README.txt"
text = p.read_text()
for old, new in (
    ("2. UP", "3. UP"), ("4. MENU", "1. MENU"), ("5. ENTER", "10. ENTER"),
    ("6. LEFT", "7. LEFT"), ("7. DOWN", "8. DOWN"), ("8. RIGHT", "9. RIGHT"),
    ("9. SPACE", "11. SPACE"), ("10. ESC", "5. ESC"),
    ("Buttons 1, 3, and 11–14 are unused.", "Buttons 2, 4, 6, and 12–14 are unused."),
    ("Example for physical button 9:", "Example for physical button 11:"),
    ("key.8.label=SPACE", "key.10.label=SPACE"),
    ("key.8.action=space", "key.10.action=space"),
    ("key.8.image=custom/page2_08.jpg", "key.10.image=custom/page2_10.jpg"),
    ("key.8.trigger=press", "key.10.trigger=press"),
):
    text = text.replace(old, new)
p.write_text(text)
