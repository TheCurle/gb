#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "headers/cpu.h"
#include "headers/disass.h"
#include "headers/lib.h"
#include "headers/banking.h"
#include "headers/instr.h"
#include "headers/debug.h"

uint8_t read_mem(uint16_t address, Cpu *cpu);
bool is_set(uint8_t reg, uint8_t bit);


// fix sprites next 



// setup a fresh cpu state and return it to main
Cpu init_cpu(void) // <--- memory should be randomized on startup
{	
	Cpu cpu;
	cpu.mem = calloc(0x10000, sizeof(uint8_t)); // main memory
	cpu.ram_banks = calloc(0x8000,sizeof(uint8_t)); // ram banks
	cpu.currentram_bank = 0;
	cpu.currentrom_bank = 1; // all ways at least one
	//different values for the test!?
	cpu.af.reg = 0x01B0; // <-- correct values
	cpu.bc.reg = 0x0013;
	cpu.de.reg = 0x00D8;
	cpu.hl.reg = 0x014d;
	cpu.sp = 0xFFFE;
	
	/*
	cpu.af.reg = 0x1180;
	cpu.bc.reg = 0x0000;
	cpu.de.reg = 0xff56;
	cpu.hl.reg = 0x000d;
	cpu.sp = 0xfffe;
	*/
	cpu.mem[0xFF10] = 0x80;
	cpu.mem[0xFF11] = 0xBF;	
	cpu.mem[0xFF12] = 0xF3;
	cpu.mem[0xFF14] = 0xBF;
	cpu.mem[0xFF16] = 0x3F;
	cpu.mem[0xFF19] = 0xBF;
	cpu.mem[0xFF1A] = 0x7F;
	cpu.mem[0xFF1B] = 0xFF;
	cpu.mem[0xFF1C] = 0x9F;
	cpu.mem[0xFF1E] = 0xBF;
	cpu.mem[0xFF20] = 0xFF;
	cpu.mem[0xFF23] = 0xBF;
	cpu.mem[0xFF24] = 0x77;
	cpu.mem[0xFF25] = 0xF3;
	cpu.mem[0xFF26] = 0xF1;
	cpu.mem[0xFF40] = 0x91;
	cpu.mem[0xff41] = 0x85; // <- lcd stat reg (from bgb lol)
	cpu.mem[0xFF47] = 0xFC;
	cpu.mem[0xFF48] = 0xFF;
	cpu.mem[0xFF49] = 0xFF;
	
	cpu.mem[0xff0f] = 0xe1; // what bgb says?
	cpu.mem[0xff00] = 0xff; // all not pressed
	cpu.pc = 0x100; // reset at 0x100
	cpu.tacfreq = 256; // number of machine cycles till update
	cpu.scanline_counter = 114;
	cpu.joypad_state = 0xff;
	cpu.breakpoint = 0x100;
	return cpu;
}



void request_interrupt(Cpu * cpu,int interrupt)
{
	// set the interrupt flag to signal
	// an interrupt request
	uint8_t req = cpu->mem[0xff0f];
	set_bit(req,interrupt);
	cpu->mem[0xff0f] = req;
	//puts("Interrupt sucessfully requested");
}



void do_interrupts(Cpu *cpu)
{
	
	// if interrupts are enabled
	if(cpu->interrupt_enable)
	{	
		// get the set requested interrupts
		uint8_t req = cpu->mem[0xff0f];
		//uint8_t req = read_mem(cpu,0xff0f); // read mem appears to be returning ff may cause issues later
		//printf("req: %x : %x\n",req,cpu->mem[0xff0f]);
		// checked that the interrupt is enabled from the ie reg 
		uint8_t enabled = cpu->mem[0xffff];
		
		if(req > 0)
		{
			// priority for servicing starts at interrupt 0
			for(int i = 0; i < 5; i++)
			{
				
				// if requested
				if(is_set(req,i))
				{
					// check that the particular interrupt is enabled
					if(is_set(enabled,i))
					{
						//printf("service: %d\n", i);
						service_interrupt(cpu,i);
					}
				}
			}
		}
	}
}

inline void service_interrupt(Cpu *cpu,int interrupt)
{
	
	// no idea if this is needed may not allow interrupts to function properly...
	// needs fixing 
	if(cpu->interrupt_enable)
	{
		cpu->interrupt_enable = false; // disable interrupts now one is serviced
		
		// reset the bit of in the if to indicate it has been serviced
		uint8_t req = read_mem(0xff0f,cpu);
		deset_bit(req,interrupt);
		write_mem(cpu,0xff0f,req);
		
		// push the current pc on the stack to save it
		// it will be pulled off by reti or ret later
		write_stackw(cpu,cpu->pc);

		
		// set the program counter to the start of the
		// interrupt handler for the request interrupt
		
		switch(interrupt)
		{
			// interrupts are one less than listed in cpu manual
			// as our bit macros work from bits 0-7 not 1-8
			case 0: cpu->pc = 0x40;  break; //vblank
			case 1: cpu->pc = 0x48; break; //lcd-state 
			case 2: cpu->pc = 0x50; break; // timer 
											// needs one for completed serial transfter
			case 4: cpu->pc = 0x60; break; // joypad
		}
		
		
		
		
		//printf("Interrupt: cpu->pc = %x\n",cpu->pc);
	}
	
/*	//else
	{
		printf("Request %d ignored interrupts disabled\n", interrupt);
	}
*/	
	
}


// memory accesses (READ THE PANCDOCS ON MEMORY MAP)
// may need access to information structs

// needs ones related to banking



void write_mem(Cpu *cpu,uint16_t address,int data)
{

	// write breakpoint
	if(address == cpu->memw_breakpoint)
	{
		printf("Write breakpoint (%x)!\n",cpu->memw_breakpoint);
		cpu->memw_breakpoint = -1;
		printf("data %x\n",data);
		enter_debugger(cpu);
	}

	// do ram enabling 
	if(address <= 0x2000)
	{
		if(cpu->rom_info.mbc1 || cpu->rom_info.mbc2)
		{
			do_ram_bank_enable(cpu,address,data);
		}
	}
	
	// do rom of ram bank change
	else if(address >= 0x200 && address < 0x4000)
	{
		if(cpu->rom_info.mbc1 || cpu->rom_info.mbc2)
		{
			do_change_lo_rom_bank(cpu,data);
		}		
	}
	
	else if((address >= 0x4000) && (address < 0x6000))
	{
		// no rambank in mbc2 use rambank 0
		if(cpu->rom_info.mbc1)
		{
			if(cpu->rom_banking)
			{
				do_change_hi_rom_bank(cpu,data);
			}
			
			else
			{
				do_ram_bank_change(cpu,data);
			}
		}
	}
	
	// this changes wether we want to rom or ram bank
	// for the above
	else if((address >= 0x6000 && address < 0x8000))
	{
		if(cpu->rom_info.mbc1)
		{
			do_change_rom_ram_mode(cpu,data);
		}
	}
			
	
	

	
	// ECHO ram also writes in ram
	else if( (address >= 0xE000) && (address < 0xFE00))
	{
		cpu->mem[address] = data;
		cpu->mem[address-0x2000] = data;
	}
	
	
	// two below need imeplementing 
	
	// vram can only be accesed at mode 0-2
	else if(address >= 0x8000 && address <= 0x9fff)
	{
		uint8_t status = read_mem(0xff41,cpu);
		status &= 3; // get just the mode
		if(status <= 3)
		{
			cpu->mem[address] = data;
		}
	}

	// oam is accesible during mode 0-1
	else if(address >= 0xfe00 && address <= 0xfe9f)
	{
		uint8_t status = read_mem(0xff41,cpu);
		status &= 3; // get just the mode
		if(status <= 1)
		{
			cpu->mem[address] = data;
		}
	}

	
	// restricted 
	else if( (address >= 0xFEA0) && (address < 0xFEFF) )
	{

	}
	
	// update the timer freq
	else if(address == TMC)
	{
		
		// timer is set to CLOCKSPEED/freq/4 (4 to convert to machine cycles)
		// <--- needs verifying
		uint8_t currentfreq = cpu->mem[TMC] & 3;
		cpu->mem[address] = data;
		
		if(currentfreq != cpu->mem[TMC] & 3)
		{
			switch(data & 3)
			{
				case 0: cpu->tacfreq = 256; break; // freq 4096
				case 1: cpu->tacfreq = 4; break; //freq 262144
				case 2: cpu->tacfreq = 16; break; // freq 65536
				case 3: cpu->tacfreq = 64; break; // freq 16382
			}
		}
		
	}
	
	// writing anything to DIV resets it to zero
	else if(address == DIV)
	{
		cpu->mem[DIV] = 0;
	}
	
	// reset ly if the game tries writing to it
	else if (address == 0xFF44)
	{
		cpu->mem[address] = 0;
	} 
	
	else if(address == 0xff46) // dma reg perform a dma transfer
	{
		uint16_t address = data  << 8;
		// transfer is from 0xfe00 to 0xfea0
		for(int i = 0; i < 0xA0; i++)
		{
			write_mem(cpu,0xfe00+i, read_mem(address+i,cpu));
		}
	}
	
	
	/*if(address == 0xff0f)
	{
		printf("write to if: %x\n",data);
		cpu->mem[address] = data;
	}
	*/
/*	else if(address == 0xff00)
	{
		printf("write to joypad %x\n",data);
		cpu->mem[address] = (207 | data);
		printf("emu wrote %x\n",207 | data);
	}
*/	
	
	// unrestricted
	else
	{
		cpu->mem[address] = data;
	}
}


void write_word(Cpu *cpu,uint16_t address,int data) // <- verify 
{
	//printf("data: %x, [%x]%x, [%x]%x\n", data,address,data & 0xff,address+1, (data & 0xff00) >> 8);
	write_mem(cpu,address+1,((data & 0xff00) >> 8));
	write_mem(cpu,address,(data & 0x00ff));
}

// needs reads related to banking after tetris
// also needs the vram related stuff 
uint8_t read_mem(uint16_t address, Cpu *cpu)
{
	
	// read breakpoint
	if(address == cpu->memr_breakpoint)
	{
		printf("read breakpoint (%x)!\n",cpu->memr_breakpoint);
		cpu->memr_breakpoint = -1;
		enter_debugger(cpu);
	}
	
	
	// are we reading from a rom bank
	if((address >= 0x4000) && (address <= 0x7FFF))
	{
		uint16_t new_address = address - 0x4000;
		return cpu->rom_mem[new_address + (cpu->currentrom_bank*0x4000)];
	}
		
	
	// are we reading from a ram bank
	else if((address >= 0xa0000) && (address <= 0xbfff))
	{
		uint16_t new_address = address - 0xa0000;
		return cpu->ram_banks[new_address + (cpu->currentram_bank * 0x2000)];
	}
	
	

	
	
	
	
	else if(address == 0xff00) // joypad control reg <-- cleary bugged
	{
		//puts("Joypad read:");
		//cpu_state(cpu);
		
		// read from mem
		uint8_t req = cpu->mem[0xff00];
		//printf("Joypad req = %x\n",req);
		// we want to test if bit 5 and 4 are set 
		// so we can determine what the game is interested
		// in reading
		
		
		
		// read out dpad 
		if(!is_set(req,4))
		{
			return ( (req & 0xf0) | (cpu->joypad_state & 0xf) );
		}
		// read out a b sel start 
		else if(!is_set(req,5))
		{
			return ( (req & 0xf0) | ((cpu->joypad_state >> 4) & 0xf ) );
		}		
			
		return 0xff; // return all unset
		
	}	
	
	
	
	else
	{
		return cpu->mem[address]; // just a stub for now implement later
	}
}



// checked memory reads
uint16_t read_word(int address, const Cpu *cpu)
{
	return read_mem(address,cpu) | (read_mem(address+1,cpu) << 8);
}

// implement the memory stuff for stack minips


void write_stack(Cpu *cpu, uint8_t data)
{
	cpu->sp -= 1;
	//printf("write to stack [%x] = %x\n",cpu->sp,data);
	write_mem(cpu,cpu->sp,data); // write to stack
	// and decrement stack pointer
	//printf("stack now located at [%x]\n",cpu->sp);
}


void write_stackw(Cpu *cpu,uint16_t data)
{
	write_stack(cpu,(data & 0xff00) >> 8);
	write_stack(cpu,(data &0x00ff));
}

uint8_t read_stack(Cpu *cpu)
{
	
	uint8_t data = read_mem(cpu->sp,cpu);
	cpu->sp += 1;
	return data;
}


uint16_t read_stackw(Cpu *cpu)
{
	return read_stack(cpu) | (read_stack(cpu) << 8);
}


// globals fix later
int divcounter = 64;
int timercounter = 256; // we are doing this in machine cycles




//void update_graphics(Cpu *cpu, int cycles) todo



// updates the timers
void update_timers(Cpu *cpu, int cycles)
{
	
	// divider reg in here for convenience
	divcounter += cycles;
	if(divcounter > 64)
	{
		divcounter = 0; // inc rate
		cpu->mem[DIV]++; // inc the div timer 
						
	}
	
	// if timer is enabled
	if(is_set(cpu->mem[TMC],2))
	{	
		timercounter -= cycles;
		
		// update tima register cycles have elapsed
		if(timercounter <= 0)
		{
			timercounter = cpu->tacfreq;
			
			// about to overflow
			if(cpu->mem[TIMA] == 255)
			{	
				cpu->mem[TIMA] = cpu->mem[TMA]; // reset to value in tma
				//puts("Timer overflow!");
				request_interrupt(cpu,2); // timer overflow interrupt
			}
			
			else
			{
				cpu->mem[TIMA]++;
			}
		}
	}
}




