#ifndef HTML_PRINTER_H
#define HTML_PRINTER_H

#include "printer.h"

#include "printer_emulator.h"

class htmlPrinter : public printer_emu
{
private:
    bool inverse = false;

protected:
    virtual void post_new_file() override;
    virtual void pre_close_file() override;
    virtual bool process_buffer(uint8_t linelen, uint8_t aux1, uint8_t aux2) override;

public:
    htmlPrinter(paper_t ptype=HTML) { _paper_type = ptype; };

    const char *modelname() override  
    { 
        if (_paper_type == HTML)
        {
                return rs232Printer::printer_model_str[rs232Printer::PRINTER_HTML];
        }
        if (_paper_type == HTML_ATASCII)
        {
                return rs232Printer::printer_model_str[rs232Printer::PRINTER_HTML_ATASCII];
        }
        return PRINTER_UNSUPPORTED;
  
    };
};

#endif
