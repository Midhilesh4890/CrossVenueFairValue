import fairvaluelab


def test_package_version() -> None:
    assert fairvaluelab.__version__ == "0.1.0"
    assert fairvaluelab.__all__ == ["__version__"]
