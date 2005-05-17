/*****************************************************************************\
 *  prefix_courier.h - message packer for length-prefixed packets.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef __PREFIX_COURIER_H__
#define __PREFIX_COURIER_H__

#include "courier.h"
#include "mailbag.h"

// **************************************************************************
//  TAG(                      prefix_courier_t                         ) 
//  
// Specialization of the courier_t for Maui Wiki sessions in which the
// content is framed by prefixing it with length data, as in
//
//	00000325\n
//	<stuff>
//
// **************************************************************************
class prefix_courier_t : public courier_t
{
public:
    
	prefix_courier_t( const int fd,
			  const mailbag_factory_t * const factory ) :
		courier_t( fd, factory )
	{
	}

	virtual mailbag_t *receive( void );
	virtual int send( mailbag_t *bag );
};


// **************************************************************************
//  TAG(                    prefix_courier_factory_t                    ) 
// **************************************************************************
class prefix_courier_factory_t : public courier_factory_t
{
public:
	courier_t *courier( int fd,
			    const mailbag_factory_t * const factory ) const
	{
		return new prefix_courier_t( fd, factory );
	}
};


#endif /*__PREFIX_COURIER_H__*/
