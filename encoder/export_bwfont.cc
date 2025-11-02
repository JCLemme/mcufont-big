#include "export_bwfont.hh"
#include <iostream>
#include <vector>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>
#include <string>
#include <cctype>
#include "exporttools.hh"
#include "importtools.hh"
#include "ccfixes.hh"

#define BWFONT_FORMAT_VERSION 4
#define TYPECASE_FORMAT_VERSION 2

namespace mcufont {
namespace bwfont {

static void encode_glyph(const DataFile::glyphentry_t &glyph,
                         const DataFile::fontinfo_t &fontinfo,
                         std::vector<unsigned> &dest,
                         int num_cols)
{
    const int threshold = 8;

    if (glyph.data.size() == 0)
        return;

    // Find the number of columns in the glyph data
    if (num_cols == 0)
    {
        for (int x = 0; x < fontinfo.max_width; x++)
        {
            for (int y = 0; y < fontinfo.max_height; y++)
            {
                size_t index = y * fontinfo.max_width + x;
                if (glyph.data.at(index) >= threshold)
                    num_cols = x + 1;
            }
        }
    }

    // Write the bits that compose the glyph
    for (int x = 0; x < num_cols; x++)
    {
        for (int y = 0; y < fontinfo.max_height; y+= 8)
        {
            size_t remain = std::min(8, fontinfo.max_height - y);
            uint8_t byte = 0;
            for (size_t i = 0; i < remain; i++)
            {
                size_t index = (y + i) * fontinfo.max_width + x;
                if (glyph.data.at(index) >= threshold)
                {
                    byte |= (1 << i);
                }
            }
            dest.push_back(byte);
        }
    }
}

struct cropinfo_t
{
    size_t offset_x;
    size_t offset_y;
    size_t height_bytes;
    size_t height_pixels;
    size_t width;
};

static void encode_character_range(std::ostream &out,
                                   const std::string &name,
                                   const DataFile &datafile,
                                   const char_range_t &range,
                                   unsigned range_index,
                                   cropinfo_t &cropinfo)
{
    std::vector<DataFile::glyphentry_t> glyphs;
    bool constant_width = true;
    int width = datafile.GetGlyphEntry(range.glyph_indices[0]).width;

    // Copy all the glyphs in this range for the purpose of cropping them.
    for (int glyph_index: range.glyph_indices)
    {
        if (glyph_index < 0)
        {
            // Missing glyph
            DataFile::glyphentry_t dummy = {};
            glyphs.push_back(dummy);
        }
        else
        {
            auto glyph = datafile.GetGlyphEntry(glyph_index);
            glyphs.push_back(glyph);

            if (glyph.width != width)
            {
                constant_width = false;
                width = 0;
            }
        }
    }

    // Crop the glyphs in this range. Getting rid of a few rows at top
    // or left can save a bunch of bytes with minimal cost.
    DataFile::fontinfo_t old_fi = datafile.GetFontInfo();
    DataFile::fontinfo_t new_fi = old_fi;
    crop_glyphs(glyphs, new_fi);

    if (new_fi.max_width != width)
    {
        constant_width = false;
        width = 0;
    }

    // Fill in the crop information
    cropinfo.offset_x = old_fi.baseline_x - new_fi.baseline_x;
    cropinfo.offset_y = old_fi.baseline_y - new_fi.baseline_y;
    cropinfo.height_pixels = new_fi.max_height;
    cropinfo.height_bytes = (cropinfo.height_pixels + 7) / 8;
    cropinfo.width = width;

    // Then format and write out the glyph data
    std::vector<unsigned> offsets;
    std::vector<unsigned> data;
    std::vector<unsigned> widths;
    size_t stride = cropinfo.height_bytes;

    for (const DataFile::glyphentry_t &g : glyphs)
    {
        offsets.push_back(data.size() / stride);
        widths.push_back(g.width);
        encode_glyph(g, new_fi, data, width);
    }
    offsets.push_back(data.size() / stride);

    write_const_table(out, data, "uint8_t", "mf_bwfont_" + name + "_glyph_data_" + std::to_string(range_index), 1);

    if (!constant_width)
    {
        write_const_table(out, offsets, "uint16_t", "mf_bwfont_" + name + "_glyph_offsets_" + std::to_string(range_index), 1, 4);
        write_const_table(out, widths, "uint8_t", "mf_bwfont_" + name + "_glyph_widths_" + std::to_string(range_index), 1);
    }
}

void write_source(std::ostream &out, std::string name, const DataFile &datafile)
{
    name = filename_to_identifier(name);

    out << std::endl;
    out << std::endl;
    out << "/* Start of automatically generated font definition for " << name << ". */" << std::endl;
    out << std::endl;

    out << "#ifndef MF_BWFONT_INTERNALS" << std::endl;
    out << "#define MF_BWFONT_INTERNALS" << std::endl;
    out << "#endif" << std::endl;
    out << "#include \"mf_bwfont.h\"" << std::endl;
    out << std::endl;

    out << "#ifndef MF_BWFONT_VERSION_" << BWFONT_FORMAT_VERSION << "_SUPPORTED" << std::endl;
    out << "#error The font file is not compatible with this version of mcufont." << std::endl;
    out << "#endif" << std::endl;
    out << std::endl;

    // Split the characters into ranges
    DataFile::fontinfo_t f = datafile.GetFontInfo();
    size_t glyph_size = f.max_width * ((f.max_height + 7) / 8);
    auto get_glyph_size = [=](size_t i) { return glyph_size; };
    std::vector<char_range_t> ranges = compute_char_ranges(datafile,
        get_glyph_size, 65536, 16);

    // Write out glyph data for character ranges
    std::vector<cropinfo_t> crops;
    for (size_t i = 0; i < ranges.size(); i++)
    {
        cropinfo_t cropinfo;
        encode_character_range(out, name, datafile, ranges.at(i), i, cropinfo);
        crops.push_back(cropinfo);
    }

    // Write out a table describing the character ranges
    out << "static const struct mf_bwfont_char_range_s mf_bwfont_" + name + "_char_ranges[] = {" << std::endl;
    for (size_t i = 0; i < ranges.size(); i++)
    {
        std::string offsets = (crops[i].width) ? "0" : "mf_bwfont_" + name + "_glyph_offsets_" + std::to_string(i);
        std::string widths = (crops[i].width) ? "0" : "mf_bwfont_" + name + "_glyph_widths_" + std::to_string(i);

        out << "    {" << std::endl;
        out << "        " << ranges.at(i).first_char << ", /* first char */" << std::endl;
        out << "        " << ranges.at(i).char_count << ", /* char count */" << std::endl;
        out << "        " << crops[i].offset_x << ", /* offset x */" << std::endl;
        out << "        " << crops[i].offset_y << ", /* offset y */" << std::endl;
        out << "        " << crops[i].height_bytes << ", /* height in bytes */" << std::endl;
        out << "        " << crops[i].height_pixels << ", /* height in pixels */" << std::endl;
        out << "        " << crops[i].width << ", /* width */" << std::endl;
        out << "        " << widths << ", /* glyph widths */" << std::endl;
        out << "        " << offsets << ", /* glyph offsets */" << std::endl;
        out << "        " << "mf_bwfont_" << name << "_glyph_data_" << i << ", /* glyph data */" << std::endl;
        out << "    }," << std::endl;
    }
    out << "};" << std::endl;
    out << std::endl;

    // Fonts in this format are always black & white
    int flags = datafile.GetFontInfo().flags | DataFile::FLAG_BW;

    // Pull it all together in the rlefont_s structure.
    out << "const struct mf_bwfont_s mf_bwfont_" << name << " = {" << std::endl;
    out << "    {" << std::endl;
    out << "    " << "\"" << datafile.GetFontInfo().name << "\"," << std::endl;
    out << "    " << "\"" << name << "\"," << std::endl;
    out << "    " << datafile.GetFontInfo().max_width << ", /* width */" << std::endl;
    out << "    " << datafile.GetFontInfo().max_height << ", /* height */" << std::endl;
    out << "    " << get_min_x_advance(datafile) << ", /* min x advance */" << std::endl;
    out << "    " << get_max_x_advance(datafile) << ", /* max x advance */" << std::endl;
    out << "    " << datafile.GetFontInfo().baseline_x << ", /* baseline x */" << std::endl;
    out << "    " << datafile.GetFontInfo().baseline_y << ", /* baseline y */" << std::endl;
    out << "    " << datafile.GetFontInfo().line_height << ", /* line height */" << std::endl;
    out << "    " << flags << ", /* flags */" << std::endl;
    out << "    " << select_fallback_char(datafile) << ", /* fallback character */" << std::endl;
    out << "    " << "&mf_bwfont_character_width," << std::endl;
    out << "    " << "&mf_bwfont_render_character," << std::endl;
    out << "    }," << std::endl;

    out << "    " << BWFONT_FORMAT_VERSION << ", /* version */" << std::endl;
    out << "    " << ranges.size() << ", /* char range count */" << std::endl;
    out << "    " << "mf_bwfont_" << name << "_char_ranges," << std::endl;
    out << "};" << std::endl;

    // Write the font lookup structure
    out << std::endl;
    out << "#ifdef MF_INCLUDED_FONTS" << std::endl;
    out << "/* List entry for searching fonts by name. */" << std::endl;
    out << "static const struct mf_font_list_s mf_bwfont_" << name << "_listentry = {" << std::endl;
    out << "    MF_INCLUDED_FONTS," << std::endl;
    out << "    (struct mf_font_s*)&mf_bwfont_" << name << std::endl;
    out << "};" << std::endl;
    out << "#undef MF_INCLUDED_FONTS" << std::endl;
    out << "#define MF_INCLUDED_FONTS (&mf_bwfont_" << name << "_listentry)" << std::endl;
    out << "#endif" << std::endl;

    out << std::endl;
    out << std::endl;
    out << "/* End of automatically generated font definition for " << name << ". */" << std::endl;
    out << std::endl;
}




static void encode_character_range_raw(std::vector<std::vector<unsigned>>& blocks,
                                   const DataFile &datafile,
                                   const char_range_t &range,
                                   unsigned range_index,
                                   cropinfo_t &cropinfo)
{
    std::vector<DataFile::glyphentry_t> glyphs;
    bool constant_width = true;
    int width = datafile.GetGlyphEntry(range.glyph_indices[0]).width;

    // Copy all the glyphs in this range for the purpose of cropping them.
    for (int glyph_index: range.glyph_indices)
    {
        if (glyph_index < 0)
        {
            // Missing glyph
            DataFile::glyphentry_t dummy = {};
            glyphs.push_back(dummy);
        }
        else
        {
            auto glyph = datafile.GetGlyphEntry(glyph_index);
            glyphs.push_back(glyph);

            if (glyph.width != width)
            {
                constant_width = false;
                width = 0;
            }
        }
    }

    // Crop the glyphs in this range. Getting rid of a few rows at top
    // or left can save a bunch of bytes with minimal cost.
    DataFile::fontinfo_t old_fi = datafile.GetFontInfo();
    DataFile::fontinfo_t new_fi = old_fi;
    crop_glyphs(glyphs, new_fi);

    if (new_fi.max_width != width)
    {
        constant_width = false;
        width = 0;
    }

    // Fill in the crop information
    cropinfo.offset_x = old_fi.baseline_x - new_fi.baseline_x;
    cropinfo.offset_y = old_fi.baseline_y - new_fi.baseline_y;
    cropinfo.height_pixels = new_fi.max_height;
    cropinfo.height_bytes = (cropinfo.height_pixels + 7) / 8;
    cropinfo.width = width;

    // Then format and write out the glyph data
    std::vector<unsigned> offsets;
    std::vector<unsigned> data;
    std::vector<unsigned> widths;
    size_t stride = cropinfo.height_bytes;

    for (const DataFile::glyphentry_t &g : glyphs)
    {
        offsets.push_back(data.size() / stride);
        widths.push_back(g.width);
        encode_glyph(g, new_fi, data, width);
    }
    offsets.push_back(data.size() / stride);

    if(!constant_width)
    {
        blocks.push_back(widths);
        blocks.push_back(offsets);
    }
    else
    {
        blocks.push_back({});
        blocks.push_back({});
    }

    blocks.push_back(data);
}

void write_case(std::ostream &out, std::string name, const DataFile &datafile)
{
    name = filename_to_identifier(name);

    int flags = datafile.GetFontInfo().flags | DataFile::FLAG_BW;
    uint16_t tmptwo;
    uint32_t tmpfour;
    uint32_t run = 0;

    // Split the characters into ranges
    DataFile::fontinfo_t f = datafile.GetFontInfo();
    size_t glyph_size = f.max_width * ((f.max_height + 7) / 8);
    auto get_glyph_size = [=](size_t i) { return glyph_size; };
    std::vector<char_range_t> ranges = compute_char_ranges(datafile,
        get_glyph_size, 65536, 16);

    // Write out glyph data for character ranges
    std::vector<cropinfo_t> crops;
    std::vector<std::vector<unsigned>> blocks;

    for (size_t i = 0; i < ranges.size(); i++)
    {
        cropinfo_t cropinfo;
        encode_character_range_raw(blocks, datafile, ranges.at(i), i, cropinfo);
        crops.push_back(cropinfo);
    }


    // Write the magic number and version info.
    out.write("ftbw", 4);                           run += 4;
    out.put(TYPECASE_FORMAT_VERSION);               run++;
    out.put(BWFONT_FORMAT_VERSION);                 run++;

    // Encode fields next.
    out.put(datafile.GetFontInfo().max_width);      run++;
    out.put(datafile.GetFontInfo().max_height);     run++;
    out.put(get_min_x_advance(datafile));           run++;
    out.put(get_max_x_advance(datafile));           run++;
    out.put(datafile.GetFontInfo().baseline_x);     run++;
    out.put(datafile.GetFontInfo().baseline_y);     run++;
    out.put(datafile.GetFontInfo().line_height);    run++;
    out.put(flags);                                 run++;

    // Long field. We'll be debilish and assume little endianness.
    tmptwo = select_fallback_char(datafile);
    out.write((char*)&tmptwo, sizeof(uint16_t));    run += 2;

    // Write names.
    std::string fname = datafile.GetFontInfo().name;
    out.put(fname.length());                        run++;
    out.write(fname.c_str(), fname.length());       run += fname.length();

    out.put(name.length());                         run++;
    out.write(name.c_str(), name.length());         run += name.length();

    // Character ranges, which is where it gets tricky.
    out.put(ranges.size());                         run++;

    // We'll need to know where glyph data starts, so do some thinking beforehand...
    // (if it wasn't clear, these are the sizes of all the fields in the range)
    run += (2 + 2 + 1 + 1 + 1 + 1 + 1 + 4 + 4 + 4) * ranges.size();
    std::cout << "Final offset is " << std::hex <<  run << std::endl;

    for (size_t i = 0; i < ranges.size(); i++)
    {
        std::string offsets = (crops[i].width) ? "0" : "mf_bwfont_" + name + "_glyph_offsets_" + std::to_string(i);
        std::string widths = (crops[i].width) ? "0" : "mf_bwfont_" + name + "_glyph_widths_" + std::to_string(i);

        tmptwo = ranges.at(i).first_char;
        out.write((char*)&tmptwo, sizeof(uint16_t));
        tmptwo = ranges.at(i).char_count;
        out.write((char*)&tmptwo, sizeof(uint16_t));

        out.put(crops[i].offset_x);
        out.put(crops[i].offset_y);
        out.put(crops[i].height_bytes);
        out.put(crops[i].height_pixels);
        out.put(crops[i].width);

        if(crops[i].width)
        {
            tmpfour = 0;
            out.write((char*)&tmpfour, sizeof(uint32_t));
            out.write((char*)&tmpfour, sizeof(uint32_t));
        }
        else
        {
            tmpfour = run;
            out.write((char*)&tmpfour, sizeof(uint32_t));
            run += (blocks.at((i*3)+0)).size() * sizeof(uint8_t);

            tmpfour = run;
            out.write((char*)&tmpfour, sizeof(uint32_t));
            run += (blocks.at((i*3)+1)).size() * sizeof(uint16_t);
        }

        tmpfour = run;
        out.write((char*)&tmpfour, sizeof(uint32_t));
        run += (blocks.at((i*3)+2)).size() * sizeof(uint8_t);
    }

    // All the ranges are recorded - time to blit the glyph data
    for(size_t i = 0; i < blocks.size(); i+=3)
    {
        // Lol lmao even
        for(size_t j = 0; j < blocks.at(i+0).size(); j++)
        {
            out.put(blocks.at(i+0).at(j));
        }

        // Lol lmao even
        for(size_t j = 0; j < blocks.at(i+1).size(); j++)
        {
            tmptwo = blocks.at(i+1).at(j);
            out.write((char*)&tmptwo, sizeof(uint16_t));
        }

        // Lol lmao even
        for(size_t j = 0; j < blocks.at(i+2).size(); j++)
        {
            out.put(blocks.at(i+2).at(j));
        }
    }

}


}}
