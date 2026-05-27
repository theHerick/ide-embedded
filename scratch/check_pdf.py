import sys
print("Python:", sys.version)

libs = ['pypdf', 'PyPDF2', 'fitz', 'pdf2image', 'pdfplumber', 'reportlab', 'poppler']
for lib in libs:
    try:
        __import__(lib)
        print(f"  {lib}: AVAILABLE")
    except ImportError:
        print(f"  {lib}: NOT AVAILABLE")
