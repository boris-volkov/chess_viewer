#!/usr/bin/env python3
"""
SVG to PNG Converter for Chess Pieces

This script converts SVG chess piece files to PNG format for use with chess_viewer.
It automatically detects SVG files in the pieces directory and converts them to PNG.

Requirements:
- Python 3.x
- cairosvg (will be installed automatically if missing)

Usage:
    python svg_to_png.py [size]

Arguments:
    size: Optional PNG size in pixels (default: 64)

The script will:
1. Install cairosvg if not available
2. Find all SVG files in the pieces/ directory
3. Convert them to PNG format with the specified size
4. Preserve the original naming convention (e.g., Chess_klt.svg -> Chess_klt.png)
"""

import os
import sys
import subprocess
import glob

def install_cairosvg():
    """Install cairosvg if not available."""
    try:
        import cairosvg
        return True
    except ImportError:
        print("cairosvg not found. Installing...")
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", "cairosvg"])
            print("cairosvg installed successfully.")
            return True
        except subprocess.CalledProcessError:
            print("Failed to install cairosvg. Please install it manually:")
            print("pip install cairosvg")
            return False

def convert_svg_to_png(svg_path, png_path, size):
    """Convert a single SVG file to PNG with high quality settings."""
    try:
        import cairosvg
        # Use higher DPI and scale for better quality
        dpi = 300  # High DPI for crisp rendering
        scale = 2  # Render at 2x size then scale down for anti-aliasing

        # Render at higher resolution for better quality
        cairosvg.svg2png(
            url=svg_path,
            write_to=png_path,
            output_width=size,
            output_height=size,
            dpi=dpi,
            scale=scale,
            background_color='transparent'
        )
        print(f"Converted: {os.path.basename(svg_path)} -> {os.path.basename(png_path)}")
        return True
    except Exception as e:
        print(f"Error converting {svg_path}: {e}")
        return False

def main():
    # Default size
    size = 64

    # Parse command line arguments
    if len(sys.argv) > 1:
        try:
            size = int(sys.argv[1])
        except ValueError:
            print(f"Invalid size: {sys.argv[1]}. Using default size {size}.")
    else:
        print(f"No size specified. Using default size {size}.")

    # Install cairosvg if needed
    if not install_cairosvg():
        return 1

    # Find the pieces directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    pieces_dir = os.path.join(os.path.dirname(script_dir), "pieces")

    if not os.path.exists(pieces_dir):
        print(f"Pieces directory not found: {pieces_dir}")
        return 1

    # Find all SVG files
    svg_pattern = os.path.join(pieces_dir, "*.svg")
    svg_files = glob.glob(svg_pattern)

    if not svg_files:
        print(f"No SVG files found in {pieces_dir}")
        print("Place your SVG chess piece files in the pieces/ directory.")
        print("Expected naming convention: Chess_[piece][theme].svg")
        print("Examples: Chess_klt.svg, Chess_qdt.svg, Chess_pdt.svg, etc.")
        return 1

    print(f"Found {len(svg_files)} SVG file(s) to convert.")
    print(f"Converting to PNG with size {size}x{size}...")

    converted_count = 0
    for svg_file in svg_files:
        # Generate PNG filename
        base_name = os.path.basename(svg_file)
        name_without_ext = os.path.splitext(base_name)[0]
        png_file = os.path.join(pieces_dir, f"{name_without_ext}.png")

        # Convert
        if convert_svg_to_png(svg_file, png_file, size):
            converted_count += 1

    print(f"\nConversion complete! {converted_count}/{len(svg_files)} files converted successfully.")

    if converted_count > 0:
        print("\nPNG files are ready for use with chess_viewer.")
        print("Original SVG files are preserved alongside the PNG files.")
        print("You can now run chess_viewer and it will use the new PNG pieces.")

    return 0

if __name__ == "__main__":
    sys.exit(main())