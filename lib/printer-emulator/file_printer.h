#ifndef FILE_PRINTER_H
#define FILE_PRINTER_H

#include "printer.h"

#include "printer_emulator.h"

class filePrinter : public printer_emu
{
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override {};
    virtual void pre_close_file() override {};

public:
    filePrinter(paper_t ptype=TRIM) { _paper_type = ptype; };

    const char *modelname()  override 
    { 
        if (_paper_type == ASCII)
        {
                return rs232Printer::printer_model_str[rs232Printer::PRINTER_FILE_ASCII];
        }
        else if (_paper_type == RAW)
        {
                return rs232Printer::printer_model_str[rs232Printer::PRINTER_FILE_RAW];
        }
        else
        {
                return rs232Printer::printer_model_str[rs232Printer::PRINTER_FILE_TRIM];
        }

    };
};

#endif
