import zipfile
import os
import argparse

def bundle_dictionary(html_path, resources_dir, output_path):
    print(f"Bundling {html_path} and resources from {resources_dir} into {output_path}...")
    
    with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as z:
        # Add HTML file to the root of the zip
        z.write(html_path, os.path.basename(html_path))
        print(f"Added {os.path.basename(html_path)}")
        
        # Add all resources from the resources directory
        if os.path.exists(resources_dir):
            for root, dirs, files in os.walk(resources_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    # Relative path within the zip
                    rel_path = os.path.relpath(file_path, resources_dir)
                    # Some dictionaries expect images at root, others in subfolders.
                    # This script preserves the structure under resources_dir.
                    z.write(file_path, rel_path)
            print(f"Added resources from {resources_dir}")
        else:
            print(f"Warning: Resources directory {resources_dir} not found.")

    print("Done!")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Bundle HTML and resources into a .diction file")
    parser.add_argument("--html", required=True, help="Path to the .txt.html file")
    parser.add_argument("--resources", required=True, help="Path to the folder containing images/sounds")
    parser.add_argument("--output", required=True, help="Path to the output .diction file")
    
    args = parser.parse_args()
    bundle_dictionary(args.html, args.resources, args.output)
