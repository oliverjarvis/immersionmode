import pytest

from jpreader_tools import screen

LISTING = (
    "671\tAzahar 2126.1.2 | Pokémon Ultra Moon\t1200\t616\n"
    "675\tAzahar 2126.1.2 | Pokémon Ultra Moon | Secondary Window\t100\t62\n"
    "672\t\t500\t500\n"
    "garbage line\n"
)


def test_parse_windows():
    assert screen.parse_windows(LISTING) == [
        ("671", "Azahar 2126.1.2 | Pokémon Ultra Moon", 1200, 616),
        ("675", "Azahar 2126.1.2 | Pokémon Ultra Moon | Secondary Window", 100, 62),
        ("672", "", 500, 500),
    ]


def test_pick_window_takes_the_main_game_window():
    assert screen.pick_window(screen.parse_windows(LISTING)) == "671"


def test_pick_window_without_a_game():
    with pytest.raises(screen.ScreenError):
        screen.pick_window([("1", "Finder", 100, 100)])


def test_colour_fraction(tmp_path):
    from PIL import Image

    image = Image.new("RGB", (100, 100), (0, 0, 0))
    for x in range(50):
        for y in range(100):
            image.putpixel((x, y), (0xF5, 0xEF, 0xE6))     # within tolerance of the panel colour
    path = tmp_path / "half.png"
    image.save(path)
    assert screen.colour_fraction(path) == pytest.approx(0.5, abs=0.02)
    assert screen.colour_fraction(path, rgb=(0, 0, 0)) == pytest.approx(0.5, abs=0.02)


def test_colour_fraction_counts_the_card_panel_too(tmp_path):
    from PIL import Image

    image = Image.new("RGB", (100, 100), (0, 0, 0))
    for x in range(100):
        for y in range(100):
            if x < 50:
                image.putpixel((x, y), (0xF4, 0xF0, 0xE6))   # the background
            elif x < 75:
                image.putpixel((x, y), (0xFB, 0xF8, 0xF1))   # the card panel
    path = tmp_path / "panel.png"
    image.save(path)
    assert screen.colour_fraction(path) == pytest.approx(0.75, abs=0.02)
