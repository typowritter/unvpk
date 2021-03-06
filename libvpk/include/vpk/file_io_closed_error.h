/**
 * unvpk - list, check and extract vpk archives
 * Copyright (C) 2011  Mathias Panzenböck <grosser.meister.morti@gmx.net>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef VPK_FILE_IO_CLOSED_ERROR_H
#define VPK_FILE_IO_CLOSED_ERROR_H

#include <string.h>

#include <vpk/exception.h>

namespace Vpk {
	class FileIOClosedError : public Exception {
	public:
		FileIOClosedError() : Exception("File reader is closed.") {}
		FileIOClosedError(const char *msg) : Exception(msg) {}
		FileIOClosedError(const std::string &msg) : Exception(msg) {}
	};
}

#endif
